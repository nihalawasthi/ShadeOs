// Network Stack using smoltcp
#![allow(dead_code)]
use alloc::vec;
use alloc::vec::Vec;
use alloc::collections::BTreeMap;
use spin::Mutex;
use smoltcp::phy::{Device, DeviceCapabilities, Medium, RxToken, TxToken};
use smoltcp::wire::{EthernetAddress, IpAddress, Ipv4Address, IpCidr};
use smoltcp::iface::{Config, Interface, SocketSet, SocketHandle};
use smoltcp::socket::{tcp, udp, icmp};
use smoltcp::wire::{Icmpv4Repr, Icmpv4Packet, IcmpRepr, IpRepr};
use smoltcp::time::Instant;

extern "C" {
    fn serial_write(s: *const u8);
    fn serial_write_dec(s: *const u8, n: u64);
    fn serial_write_str(s: *const u8);
    
    fn timer_get_ticks() -> u64;
    // RTL8139
    fn rtl8139_transmit(data: *const u8, len: usize) -> i32;
    fn rtl8139_receive(buffer: *mut u8, max_len: usize) -> isize;
    fn rtl8139_get_mac(mac_out: *mut u8);
    // E1000
    fn e1000_transmit(data: *const u8, len: usize) -> i32;
    fn e1000_receive(buffer: *mut u8, max_len: usize) -> isize;
    fn e1000_get_mac(mac_out: *mut u8);
    // PCnet
    fn pcnet_transmit(data: *const u8, len: usize) -> i32;
    fn pcnet_receive(buffer: *mut u8, max_len: usize) -> isize;
    fn pcnet_get_mac(mac_out: *mut u8);
}

#[derive(Clone, Copy, PartialEq)]
enum DriverType {
    Rtl8139,
    E1000,
    Pcnet,
}

static mut ACTIVE_DRIVER: Option<DriverType> = None;

fn driver_transmit(data: *const u8, len: usize) {
    unsafe {
        match ACTIVE_DRIVER {
            Some(DriverType::Rtl8139) => {
                let _ = rtl8139_transmit(data, len);
            }
            Some(DriverType::E1000) => {
                let _ = e1000_transmit(data, len);
            }
            Some(DriverType::Pcnet) => {
                let _ = pcnet_transmit(data, len);
            }
            None => {}
        }
    }
}

// Unified device adapter that implements smoltcp::phy::Device for supported NICs
#[derive(Clone)]
pub enum NetDevice {
    Rtl8139 { mac: EthernetAddress },
    E1000 { mac: EthernetAddress },
    Pcnet { mac: EthernetAddress },
}

impl NetDevice {
    pub fn new_from_active_driver() -> Option<Self> {
        unsafe {
            let mut mac_bytes = [0u8; 6];
            match ACTIVE_DRIVER {
                Some(DriverType::Rtl8139) => {
                    rtl8139_get_mac(mac_bytes.as_mut_ptr());
                    Some(NetDevice::Rtl8139 { mac: EthernetAddress(mac_bytes) })
                }
                Some(DriverType::E1000) => {
                    e1000_get_mac(mac_bytes.as_mut_ptr());
                    Some(NetDevice::E1000 { mac: EthernetAddress(mac_bytes) })
                }
                Some(DriverType::Pcnet) => {
                    pcnet_get_mac(mac_bytes.as_mut_ptr());
                    Some(NetDevice::Pcnet { mac: EthernetAddress(mac_bytes) })
                }
                None => None,
            }
        }
    }

    fn mac(&self) -> EthernetAddress {
        match *self {
            NetDevice::Rtl8139 { mac } => mac,
            NetDevice::E1000 { mac } => mac,
            NetDevice::Pcnet { mac } => mac,
        }
    }
}

pub struct RxTokenImpl {
    buffer: Vec<u8>,
}

impl RxToken for RxTokenImpl {
    fn consume<R, F>(mut self, f: F) -> R
    where
        F: FnOnce(&mut [u8]) -> R,
    {
        f(&mut self.buffer[..])
    }
}

pub struct TxTokenImpl;

impl TxToken for TxTokenImpl {
    fn consume<R, F>(self, len: usize, f: F) -> R
    where
        F: FnOnce(&mut [u8]) -> R,
    {
        let mut buffer = vec![0u8; len];
        let result = f(&mut buffer[..]);
        
        driver_transmit(buffer.as_ptr(), buffer.len());
        
        result
    }
}

impl Device for NetDevice {
    type RxToken<'a> = RxTokenImpl where Self: 'a;
    type TxToken<'a> = TxTokenImpl where Self: 'a;

    fn receive(&mut self, _timestamp: Instant) -> Option<(Self::RxToken<'_>, Self::TxToken<'_>)> {
        let mut buffer = vec![0u8; 2048];
        let len = unsafe {
            match *self {
                NetDevice::Rtl8139 { .. } => rtl8139_receive(buffer.as_mut_ptr(), buffer.len()),
                NetDevice::E1000 { .. } => e1000_receive(buffer.as_mut_ptr(), buffer.len()),
                NetDevice::Pcnet { .. } => pcnet_receive(buffer.as_mut_ptr(), buffer.len()),
            }
        };

        if len > 0 {
            buffer.truncate(len as usize);
            Some((RxTokenImpl { buffer }, TxTokenImpl))
        } else {
            None
        }
    }

    fn transmit(&mut self, _timestamp: Instant) -> Option<Self::TxToken<'_>> {
        Some(TxTokenImpl)
    }

    fn capabilities(&self) -> DeviceCapabilities {
        let mut caps = DeviceCapabilities::default();
        caps.max_transmission_unit = 1500;
        caps.medium = Medium::Ethernet;
        caps
    }
}

// Socket management
const MAX_SOCKETS: usize = 64;

#[derive(Clone, Copy, PartialEq)]
enum SocketType {
    Tcp,
    Udp,
    Icmp,
}

#[derive(Clone, Copy, PartialEq)]
enum SocketState {
    Closed,
    Open,
    Listening,
    Connected,
}

struct SocketEntry {
    socket_type: SocketType,
    handle: SocketHandle,
    state: SocketState,
    listening_socket: Option<SocketHandle>,
}

pub struct NetworkStack {
    device: NetDevice,
    interface: Interface,
    sockets: SocketSet<'static>,
    socket_map: BTreeMap<i32, SocketEntry>,
    next_fd: i32,
}

static NETWORK_STACK: Mutex<Option<NetworkStack>> = Mutex::new(None);

impl NetworkStack {
    pub fn new() -> Self {
        let mut device = NetDevice::new_from_active_driver()
            .expect("ACTIVE_DRIVER must be set before creating NetworkStack");
        let mac = device.mac();
        
        // Create interface configuration
        let config = Config::new(mac.into());
        
        let mut iface = Interface::new(config, &mut device.clone(), Instant::from_millis(0));
        
        // Set IP address (default for QEMU user networking)
        iface.update_ip_addrs(|ip_addrs| {
            ip_addrs.push(IpCidr::new(IpAddress::v4(10, 0, 2, 15), 24)).unwrap();
        });
        
        // Set default gateway
        iface.routes_mut().add_default_ipv4_route(Ipv4Address::new(10, 0, 2, 2)).unwrap();
        
        unsafe {
            serial_write(b"[NETWORK] Stack initialized with IP 10.0.2.15\n\0".as_ptr());
        }
        
        Self {
            device,
            interface: iface,
            sockets: SocketSet::new(vec![]),
            socket_map: BTreeMap::new(),
            next_fd: 1,
        }
    }
    
    pub fn poll(&mut self) {
        // timer_get_ticks runs at 100 Hz (10 ms per tick). Convert to milliseconds for smoltcp.
        let ticks = unsafe { timer_get_ticks() } as i64;
        let timestamp = Instant::from_millis(ticks * 10);
        let _ = self.interface.poll(timestamp, &mut self.device, &mut self.sockets);
    }

    pub fn icmp_ping(&mut self, ip: [u8; 4], timeout_ms: i32) -> i32 {
        unsafe {
            serial_write(b"[PING] start ip=\0".as_ptr());
            serial_write_dec(b"\0".as_ptr(), ip[0] as u64);
            serial_write(b".\0".as_ptr());
            serial_write_dec(b"\0".as_ptr(), ip[1] as u64);
            serial_write(b".\0".as_ptr());
            serial_write_dec(b"\0".as_ptr(), ip[2] as u64);
            serial_write(b".\0".as_ptr());
            serial_write_dec(b"\0".as_ptr(), ip[3] as u64);
        }
        
        // Create ICMP socket
        let rx_buf = icmp::PacketBuffer::new(vec![icmp::PacketMetadata::EMPTY; 4], vec![0u8; 1024]);
        let tx_buf = icmp::PacketBuffer::new(vec![icmp::PacketMetadata::EMPTY; 4], vec![0u8; 1024]);
        let mut icmp_sock = icmp::Socket::new(rx_buf, tx_buf);
        // Bind with an identifier; smoltcp permits Ident binding for echo matching
        icmp_sock.bind(icmp::Endpoint::Ident(0x1234)).ok();

        let handle = self.sockets.add(icmp_sock);

        // Prepare payload
        let payload: [u8; 8] = [0,1,2,3,4,5,6,7];

        // Convert ticks (100 Hz) to milliseconds
        let ticks_start = unsafe { timer_get_ticks() };
        let start_ms = (ticks_start as i64) * 10;
        let deadline = start_ms + timeout_ms as i64;
        let dest = IpAddress::v4(ip[0], ip[1], ip[2], ip[3]);
        unsafe { serial_write(b"[PING] start_ms=\0".as_ptr()); serial_write_dec(b"\0".as_ptr(), start_ms as u64); }
        unsafe { serial_write(b"[PING] deadline=\0".as_ptr()); serial_write_dec(b"\0".as_ptr(), deadline as u64); }
        unsafe { serial_write(b"[PING] timeout_ms=\0".as_ptr()); serial_write_dec(b"\0".as_ptr(), timeout_ms as u64); }

        // We allow a couple of retransmissions to cover ARP resolution delays.
        let mut rtt_ms: i32 = -1;
        let mut last_tx_ms = start_ms - 100000; // ensure immediate first send
        let mut sent = 0;
        let mut iterations = 0;
        loop {
            let ticks_now = unsafe { timer_get_ticks() };
            let now = (ticks_now as i64) * 10;
            iterations += 1;
            
            if now >= deadline {
                unsafe { serial_write(b"[PING] Timeout reached now=\0".as_ptr()); serial_write_dec(b"\0".as_ptr(), now as u64); }
                unsafe { serial_write(b"[PING] deadline=\0".as_ptr()); serial_write_dec(b"\0".as_ptr(), deadline as u64); }
                unsafe { serial_write(b"[PING] iterations=\0".as_ptr()); serial_write_dec(b"\0".as_ptr(), iterations as u64); }
                break;
            }

            // (Re)send every ~250ms up to 3 times
            if sent < 3 && now - last_tx_ms >= 250 {
                let socket = self.sockets.get_mut::<icmp::Socket>(handle);
                if socket.can_send() {
                    match socket.send_slice(&payload, dest) {
                        Ok(_) => {
                            unsafe { serial_write(b"[PING] sent #\0".as_ptr()); serial_write_dec(b"\0".as_ptr(), (sent + 1) as u64); }
                            last_tx_ms = now;
                            sent += 1;
                        }
                        Err(e) => {
                            unsafe { serial_write(b"[PING] send failed\n\0".as_ptr()); }
                        }
                    }
                } else {
                    unsafe { serial_write(b"[PING] cannot send yet\n\0".as_ptr()); }
                }
            }

            self.poll();

            let socket = self.sockets.get_mut::<icmp::Socket>(handle);
            if socket.can_recv() {
                match socket.recv() {
                    Ok((data, from)) => {
                        unsafe { serial_write(b"[PING] recv len=\0".as_ptr()); serial_write_dec(b"\0".as_ptr(), data.len() as u64); }
                        if data.len() >= payload.len() {
                            rtt_ms = (now - start_ms) as i32;
                            break;
                        }
                    }
                    Err(e) => {
                        // Continue polling
                    }
                }
            }
            
            // Small delay to prevent tight loop
            if iterations % 10 == 0 {
                unsafe {
                    extern "C" { fn pause(); }
                    pause();
                }
            }
        }

        unsafe { serial_write(b"[PING] rtt_ms=\0".as_ptr()); serial_write_dec(b"\0".as_ptr(), rtt_ms as u64); }

        // Cleanup
        self.sockets.remove(handle);
        rtt_ms
    }
    
    pub fn create_tcp_socket(&mut self) -> i32 {
        let rx_buffer = tcp::SocketBuffer::new(vec![0; 4096]);
        let tx_buffer = tcp::SocketBuffer::new(vec![0; 4096]);
        let socket = tcp::Socket::new(rx_buffer, tx_buffer);
        
        let handle = self.sockets.add(socket);
        let fd = self.next_fd;
        self.next_fd += 1;
        
        self.socket_map.insert(fd, SocketEntry {
            socket_type: SocketType::Tcp,
            handle,
            state: SocketState::Open,
            listening_socket: None,
        });
        
        fd
    }
    
    pub fn create_udp_socket(&mut self) -> i32 {
        let rx_buffer = udp::PacketBuffer::new(vec![udp::PacketMetadata::EMPTY; 16], vec![0; 4096]);
        let tx_buffer = udp::PacketBuffer::new(vec![udp::PacketMetadata::EMPTY; 16], vec![0; 4096]);
        let socket = udp::Socket::new(rx_buffer, tx_buffer);
        
        let handle = self.sockets.add(socket);
        let fd = self.next_fd;
        self.next_fd += 1;
        
        self.socket_map.insert(fd, SocketEntry {
            socket_type: SocketType::Udp,
            handle,
            state: SocketState::Open,
            listening_socket: None,
        });
        
        fd
    }
    
    pub fn tcp_connect(&mut self, fd: i32, ip: &[u8; 4], port: u16) -> i32 {
        let handle = if let Some(entry) = self.socket_map.get(&fd) {
            if entry.socket_type != SocketType::Tcp {
                return -1;
            }
            entry.handle
        } else {
            return -1;
        };

        {
            let socket = self.sockets.get_mut::<tcp::Socket>(handle);
            let remote_addr = smoltcp::wire::IpAddress::v4(ip[0], ip[1], ip[2], ip[3]);
            let local_port = 49152 + (fd as u16 % 16384);
            
            if socket.connect(self.interface.context(), (remote_addr, port), local_port).is_err() {
                return -1;
            }
        }

        // Drive handshake until established or short timeout (~1500ms)
        let start_ms = (unsafe { timer_get_ticks() } as i64) * 10;
        let deadline = start_ms + 1500;
        loop {
            let now = (unsafe { timer_get_ticks() } as i64) * 10;
            if now >= deadline { break; }
            self.poll();
            if self.sockets.get::<tcp::Socket>(handle).is_active() { return 0; }
        }
        -1
    }
    
    pub fn tcp_bind(&mut self, fd: i32, port: u16) -> i32 {
        if let Some(entry) = self.socket_map.get_mut(&fd) {
            if entry.socket_type != SocketType::Tcp {
                return -1;
            }
            
            let socket = self.sockets.get_mut::<tcp::Socket>(entry.handle);
            match socket.listen(port) {
                Ok(_) => {
                    entry.state = SocketState::Listening;
                    0
                }
                Err(_) => -1,
            }
        } else {
            -1
        }
    }
    
    pub fn tcp_send(&mut self, fd: i32, data: &[u8]) -> isize {
        let handle = if let Some(entry) = self.socket_map.get(&fd) {
            if entry.socket_type != SocketType::Tcp { 
                unsafe { serial_write(b"[TCP] send: not TCP socket\n\0".as_ptr()); }
                return -1; 
            }
            entry.handle
        } else { 
            unsafe { serial_write(b"[TCP] send: invalid fd\n\0".as_ptr()); }
            return -1; 
        };

        // Ensure socket is active before sending
        {
            let socket = self.sockets.get::<tcp::Socket>(handle);
            if !socket.is_active() {
                unsafe { 
                    serial_write(b"[TCP] send: socket not active, polling...\n\0".as_ptr()); 
                }
                // Drive stack to establish connection
                for _ in 0..50 {
                    self.poll();
                    if self.sockets.get::<tcp::Socket>(handle).is_active() {
                        break;
                    }
                }
            }
        }

        // Try to send with polling retries to handle WouldBlock
        let mut total_sent: usize = 0;
        let start_ms = (unsafe { timer_get_ticks() } as i64) * 10;
        let deadline = start_ms + 2000; // up to 2s overall
        let mut attempts = 0;
        loop {
            attempts += 1;
            let now = (unsafe { timer_get_ticks() } as i64) * 10;
            if now >= deadline { 
                unsafe { 
                    serial_write(b"[TCP] send: timeout after attempts=\0".as_ptr()); 
                    serial_write_dec(b"\0".as_ptr(), attempts as u64);
                }
                break; 
            }

            {
                let socket = self.sockets.get_mut::<tcp::Socket>(handle);
                if !socket.is_active() {
                    unsafe { serial_write(b"[TCP] send: socket became inactive\n\0".as_ptr()); }
                    break;
                }
                if socket.may_send() {
                    match socket.send_slice(data) {
                        Ok(len) => { 
                            unsafe { 
                                serial_write(b"[TCP] send: success, len=\0".as_ptr()); 
                                serial_write_dec(b"\0".as_ptr(), len as u64);
                            }
                            total_sent += len; 
                            break; 
                        }
                        Err(e) => { 
                            unsafe { serial_write(b"[TCP] send: error, retrying...\n\0".as_ptr()); }
                        }
                    }
                }
            }

            // Drive the stack and try again
            self.poll();
            if attempts % 10 == 0 {
                unsafe { extern "C" { fn pause(); } pause(); }
            }
        }
        if total_sent > 0 { total_sent as isize } else { -1 }
    }
    
    pub fn tcp_recv(&mut self, fd: i32, buffer: &mut [u8]) -> isize {
        if let Some(entry) = self.socket_map.get(&fd) {
            if entry.socket_type != SocketType::Tcp {
                return -1;
            }
            
            let socket = self.sockets.get_mut::<tcp::Socket>(entry.handle);
            match socket.recv_slice(buffer) {
                Ok(len) => len as isize,
                Err(_) => -1,
            }
        } else {
            -1
        }
    }
    
    pub fn close_socket(&mut self, fd: i32) -> i32 {
        if let Some(entry) = self.socket_map.remove(&fd) {
            self.sockets.remove(entry.handle);
            0
        } else {
            -1
        }
    }
}

// FFI functions for C integration

// Initialize with RTL8139
#[no_mangle]
pub extern "C" fn network_init_rtl8139(io_base: u16) -> i32 {
    extern "C" { fn rtl8139_init(io_base: u16) -> i32; }
    unsafe {
        if rtl8139_init(io_base) == 0 {
            ACTIVE_DRIVER = Some(DriverType::Rtl8139);
            let stack = NetworkStack::new();
            *NETWORK_STACK.lock() = Some(stack);
            0
        } else {
            -1
        }
    }
}

// Initialize with E1000
#[no_mangle]
pub extern "C" fn network_init_e1000(mem_base: u64) -> i32 {
    extern "C" { fn e1000_init(mem_base: u64) -> i32; }
    unsafe {
        if e1000_init(mem_base) == 0 {
            ACTIVE_DRIVER = Some(DriverType::E1000);
            let stack = NetworkStack::new();
            *NETWORK_STACK.lock() = Some(stack);
            0
        } else {
            -1
        }
    }
}

// Initialize with PCnet
#[no_mangle]
pub extern "C" fn network_init_pcnet(io_base: u16) -> i32 {
    extern "C" { fn pcnet_init(io_base: u16) -> i32; }
    unsafe {
        if pcnet_init(io_base) == 0 {
            ACTIVE_DRIVER = Some(DriverType::Pcnet);
            let stack = NetworkStack::new();
            *NETWORK_STACK.lock() = Some(stack);
            0
        } else {
            -1
        }
    }
}

// Legacy function - tries RTL8139 by default
#[no_mangle]
pub extern "C" fn network_init() -> i32 {
    // Will be called after driver is initialized separately
    if unsafe { ACTIVE_DRIVER.is_some() } {
        let stack = NetworkStack::new();
        *NETWORK_STACK.lock() = Some(stack);
        0
    } else {
        -1
    }
}

#[no_mangle]
pub extern "C" fn network_poll() {
    if let Some(ref mut stack) = *NETWORK_STACK.lock() {
        stack.poll();
    }
}

#[no_mangle]
pub extern "C" fn net_icmp_ping(ip: *const u8, timeout_ms: i32) -> i32 {
    if ip.is_null() { return -1; }
    unsafe { serial_write(b"[PING] net_icmp_ping called\n\0".as_ptr()); }
    if let Some(ref mut stack) = *NETWORK_STACK.lock() {
        let ip_slice = unsafe { core::slice::from_raw_parts(ip, 4) };
        let addr = [ip_slice[0], ip_slice[1], ip_slice[2], ip_slice[3]];
        stack.icmp_ping(addr, timeout_ms)
    } else {
        unsafe { serial_write(b"[PING] NETWORK_STACK is None\n\0".as_ptr()); }
        -1
    }
}

#[no_mangle]
pub extern "C" fn sock_socket() -> i32 {
    if let Some(ref mut stack) = *NETWORK_STACK.lock() {
        stack.create_tcp_socket()
    } else {
        -1
    }
}

#[no_mangle]
pub extern "C" fn sock_connect(s: i32, ip: *const u8, port: u16) -> i32 {
    if let Some(ref mut stack) = *NETWORK_STACK.lock() {
        let ip_slice = unsafe { core::slice::from_raw_parts(ip, 4) };
        let ip_array: [u8; 4] = [ip_slice[0], ip_slice[1], ip_slice[2], ip_slice[3]];
        stack.tcp_connect(s, &ip_array, port)
    } else {
        -1
    }
}

#[no_mangle]
pub extern "C" fn sock_bind(s: i32, _ip: *const u8, port: u16) -> i32 {
    if let Some(ref mut stack) = *NETWORK_STACK.lock() {
        stack.tcp_bind(s, port)
    } else {
        -1
    }
}

#[no_mangle]
pub extern "C" fn sock_listen(_s: i32, _backlog: i32) -> i32 {
    // Already handled in bind for smoltcp
    if NETWORK_STACK.lock().is_some() {
        0
    } else {
        -1
    }
}

#[no_mangle]
pub extern "C" fn sock_accept(s: i32, out_ip: *mut u8, out_port: *mut u16) -> i32 {
    if let Some(ref mut stack) = *NETWORK_STACK.lock() {
        if let Some(listening_entry) = stack.socket_map.get(&s) {
            if listening_entry.state != SocketState::Listening {
                return -1; // Not a listening socket
            }

            // With smoltcp, the same listening socket becomes active when a connection is established.
            let socket = stack.sockets.get_mut::<tcp::Socket>(listening_entry.handle);
            if socket.is_active() {
                // Update state to connected
                if let Some(entry) = stack.socket_map.get_mut(&s) {
                    entry.state = SocketState::Connected;
                }

                if let Some(remote) = socket.remote_endpoint() {
                    unsafe {
                        if !out_ip.is_null() {
                            let ip_bytes = remote.addr.as_bytes();
                            core::ptr::copy_nonoverlapping(ip_bytes.as_ptr(), out_ip, 4);
                        }
                        if !out_port.is_null() {
                            *out_port = remote.port;
                        }
                    }
                }
                return s;
            }
        }
    }
    -1
}

#[no_mangle]
pub extern "C" fn sock_send(s: i32, buf: *const u8, len: usize) -> isize {
    if let Some(ref mut stack) = *NETWORK_STACK.lock() {
        let data = unsafe { core::slice::from_raw_parts(buf, len) };
        stack.tcp_send(s, data)
    } else {
        -1
    }
}

#[no_mangle]
pub extern "C" fn sock_recv(s: i32, buf: *mut u8, len: usize) -> isize {
    if let Some(ref mut stack) = *NETWORK_STACK.lock() {
        let buffer = unsafe { core::slice::from_raw_parts_mut(buf, len) };
        stack.tcp_recv(s, buffer)
    } else {
        -1
    }
}

#[no_mangle]
pub extern "C" fn sock_close(s: i32) -> i32 {
    if let Some(ref mut stack) = *NETWORK_STACK.lock() {
        stack.close_socket(s)
    } else {
        -1
    }
}

#[no_mangle]
pub extern "C" fn sock_set_nonblock(_s: i32, _nonblock: i32) -> i32 {
    // smoltcp sockets are non-blocking by default
    0
}

#[no_mangle]
pub extern "C" fn sock_poll(_fds: *mut i32, _nfds: i32, _events_out: *mut i32, _timeout_ms: i32) -> i32 {
    // Simplified poll - just poll the network stack
    network_poll();
    0
}
