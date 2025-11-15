// Network Stack using smoltcp
#![allow(dead_code)]
use alloc::vec;
use alloc::vec::Vec;
use alloc::collections::BTreeMap;
use spin::Mutex;
use smoltcp::phy::{Device, DeviceCapabilities, Medium, RxToken, TxToken};
use smoltcp::wire::{EthernetAddress, IpAddress, Ipv4Address, IpCidr};
use smoltcp::iface::{Config, Interface, SocketSet, SocketHandle};
use smoltcp::socket::{tcp, udp, icmp, dhcpv4};
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

// Helper: write a single byte as two hex chars + space via serial_write
unsafe fn serial_write_hex_byte(b: u8) {
    fn nib(n: u8) -> u8 {
        if n < 10 { b'0' + n } else { b'a' + (n - 10) }
    }
    let hi = nib((b >> 4) & 0xF) as u8;
    let lo = nib(b & 0xF) as u8;
    let mut buf = [hi, lo, b' ', 0u8];
    serial_write(buf.as_ptr());
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
        
        unsafe {
            serial_write(b"[DEVICE] transmit() sending packet, len=\0".as_ptr());
            serial_write_dec(b"\n\0".as_ptr(), len as u64);
        }
        
        // Dump first up to 64 bytes (increased from 32) of the transmit buffer for debugging
        unsafe {
            serial_write(b"[DEVICE] TX DUMP (first 64 bytes): \0".as_ptr());
            let dump_len = core::cmp::min(64, buffer.len());
            for i in 0..dump_len {
                serial_write_hex_byte(buffer[i]);
            }
            serial_write(b"\n\0".as_ptr());
        }

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
            // Dump first up to 64 bytes of the received frame for debugging
            let rlen = len as usize;
            let dump_len = core::cmp::min(64, rlen);
            unsafe {
                serial_write(b"[DEVICE] receive() called, got packet len=\0".as_ptr());
                serial_write_dec(b"\n\0".as_ptr(), len as u64);
                serial_write(b"[DEVICE] RX DUMP (first 64 bytes): \0".as_ptr());
                for i in 0..dump_len {
                    serial_write_hex_byte(buffer[i]);
                }
                serial_write(b"\n\0".as_ptr());
            }
            buffer.truncate(rlen);
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
        caps.max_burst_size = Some(1); // Process one packet at a time for reliability
        
        // RTL8139 doesn't support hardware checksum offloading, so compute all in software
        caps.checksum.ipv4 = smoltcp::phy::Checksum::Both; 
        caps.checksum.tcp = smoltcp::phy::Checksum::Both;  
        caps.checksum.udp = smoltcp::phy::Checksum::Both;  
        caps.checksum.icmpv4 = smoltcp::phy::Checksum::Both;
        
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
    gateway: Option<Ipv4Address>,
    dns_servers: Vec<Ipv4Address>,
}

static NETWORK_STACK: Mutex<Option<NetworkStack>> = Mutex::new(None);

impl NetworkStack {
    pub fn new() -> Self {
        // Use DHCP to automatically obtain IP configuration
        Self::new_with_dhcp()
    }
    
    pub fn new_with_dhcp() -> Self {
        let mut device = NetDevice::new_from_active_driver()
            .expect("ACTIVE_DRIVER must be set before creating NetworkStack");
        let mac = device.mac();
        
        unsafe {
            serial_write(b"[DHCP] Starting DHCP client...\n\0".as_ptr());
        }
        
        // Create interface configuration
        let mut config = Config::new(mac.into());
        config.random_seed = unsafe { timer_get_ticks() };
        
        let mut iface = Interface::new(config, &mut device.clone(), Instant::from_millis(0));
        
        // Start with link-local IP (0.0.0.0) for DHCP
        iface.update_ip_addrs(|ip_addrs| {
            ip_addrs.push(IpCidr::new(IpAddress::v4(0, 0, 0, 0), 0)).ok();
        });
        
        // Create DHCP socket
        let mut sockets = SocketSet::new(vec![]);
        let dhcp_socket = dhcpv4::Socket::new();
        let dhcp_handle = sockets.add(dhcp_socket);
        
        unsafe {
            serial_write(b"[DHCP] Sending DHCP DISCOVER...\n\0".as_ptr());
        }
        
        // Run DHCP discovery
        let start_time = unsafe { timer_get_ticks() };
        let timeout_ms = 10000; // 10 second timeout
        
        loop {
            let now_ticks = unsafe { timer_get_ticks() };
            let elapsed_ms = (now_ticks - start_time) * 10;
            
            if elapsed_ms > timeout_ms {
                unsafe {
                    serial_write(b"[DHCP] Timeout waiting for DHCP response, using fallback IP\n\0".as_ptr());
                }
                // Fallback to static IP if DHCP fails
                return Self::new_with_static_fallback();
            }
            
            let timestamp = Instant::from_millis((now_ticks * 10) as i64);
            
            // Poll the interface
            iface.poll(timestamp, &mut device, &mut sockets);
            
            // Check DHCP socket
            let dhcp_socket = sockets.get_mut::<dhcpv4::Socket>(dhcp_handle);
            
            match dhcp_socket.poll() {
                None => {
                    // No event yet, continue polling
                }
                Some(dhcpv4::Event::Configured(config)) => {
                    unsafe {
                        serial_write(b"[DHCP] Configuration received!\n\0".as_ptr());
                        serial_write(b"[DHCP] IP: \0".as_ptr());
                        let ipv4 = config.address.address();
                        let octets = ipv4.as_bytes();
                        serial_write_dec(b".\0".as_ptr(), octets[0] as u64);
                        serial_write_dec(b".\0".as_ptr(), octets[1] as u64);
                        serial_write_dec(b".\0".as_ptr(), octets[2] as u64);
                        serial_write_dec(b"\n\0".as_ptr(), octets[3] as u64);
                    }
                    
                    // Update interface with DHCP-assigned IP
                    iface.update_ip_addrs(|ip_addrs| {
                        ip_addrs.clear();
                        ip_addrs.push(IpCidr::Ipv4(config.address)).ok();
                    });
                    
                    let gateway = config.router;
                    
                    // Collect DNS servers from DHCP config
                    let dns_servers: Vec<Ipv4Address> = config.dns_servers.iter()
                        .copied()
                        .collect();
                    
                    // Set default gateway if provided
                    if let Some(gw) = gateway {
                        iface.routes_mut().add_default_ipv4_route(gw).ok();
                        unsafe {
                            serial_write(b"[DHCP] Gateway: \0".as_ptr());
                            let octets = gw.as_bytes();
                            serial_write_dec(b".\0".as_ptr(), octets[0] as u64);
                            serial_write_dec(b".\0".as_ptr(), octets[1] as u64);
                            serial_write_dec(b".\0".as_ptr(), octets[2] as u64);
                            serial_write_dec(b"\n\0".as_ptr(), octets[3] as u64);
                        }
                    }
                    
                    // Log DNS servers
                    if !dns_servers.is_empty() {
                        unsafe {
                            serial_write(b"[DHCP] DNS servers: \0".as_ptr());
                            for (i, dns) in dns_servers.iter().enumerate() {
                                if i > 0 { serial_write(b", \0".as_ptr()); }
                                let octets = dns.as_bytes();
                                serial_write_dec(b".\0".as_ptr(), octets[0] as u64);
                                serial_write_dec(b".\0".as_ptr(), octets[1] as u64);
                                serial_write_dec(b".\0".as_ptr(), octets[2] as u64);
                                serial_write_dec(b"\n\0".as_ptr(), octets[3] as u64);
                            }
                        }
                    }
                    
                    unsafe {
                        serial_write(b"[DHCP] Network configured successfully!\n\0".as_ptr());
                    }
                    
                    // Remove DHCP socket as we're done with initial configuration
                    sockets.remove(dhcp_handle);
                    
                    return Self {
                        device,
                        interface: iface,
                        sockets,
                        socket_map: BTreeMap::new(),
                        next_fd: 1,
                        gateway,
                        dns_servers,
                    };
                }
                Some(dhcpv4::Event::Deconfigured) => {
                    unsafe {
                        serial_write(b"[DHCP] Deconfigured, retrying...\n\0".as_ptr());
                    }
                }
            }
            
            // Small delay between polls
            for _ in 0..1000 {
                core::hint::spin_loop();
            }
        }
    }
    
    fn new_with_static_fallback() -> Self {
        unsafe { serial_write(b"[NETWORK] DHCP failed. Evaluating static fallback policy...\n\0".as_ptr()); }

        // Compile-time configurable fallback via Cargo feature.
        // If feature "fallback-qemu" is enabled we keep legacy 10.0.2.x defaults.
        // Otherwise we leave stack with 0.0.0.0 (unconfigured) so user can retry DHCP or set static.
        #[cfg(feature = "fallback-qemu")] {
            unsafe { serial_write(b"[NETWORK] Using legacy QEMU fallback 10.0.2.15/10.0.2.2\n\0".as_ptr()); }
            return Self::new_with_ip([10, 0, 2, 15], [10, 0, 2, 2]);
        }
        #[cfg(not(feature = "fallback-qemu"))]
        {
            unsafe { serial_write(b"[NETWORK] No hardcoded fallback. Leaving IP unconfigured (0.0.0.0).\n\0".as_ptr()); }
            // Represent unconfigured: IP 0.0.0.0, gateway 0.0.0.0
            return Self::new_with_ip([0, 0, 0, 0], [0, 0, 0, 0]);
        }
    }
    
    pub fn new_with_ip(ip: [u8; 4], gateway: [u8; 4]) -> Self {
        let mut device = NetDevice::new_from_active_driver()
            .expect("ACTIVE_DRIVER must be set before creating NetworkStack");
        let mac = device.mac();
        
        // Create interface configuration
        let mut config = Config::new(mac.into());
        config.random_seed = unsafe { timer_get_ticks() };
        
        let mut iface = Interface::new(config, &mut device.clone(), Instant::from_millis(0));
        
        // Set IP address
        iface.update_ip_addrs(|ip_addrs| {
            ip_addrs.push(IpCidr::new(IpAddress::v4(ip[0], ip[1], ip[2], ip[3]), 24)).unwrap();
        });
        
        let gateway_addr = Ipv4Address::new(gateway[0], gateway[1], gateway[2], gateway[3]);
        let has_gateway = gateway != [0, 0, 0, 0];
        
        // Set default gateway if provided
        if has_gateway {
            iface.routes_mut().add_default_ipv4_route(gateway_addr).unwrap();
        }
        
        unsafe {
            serial_write(b"[NETWORK] Stack initialized with IP \0".as_ptr());
            serial_write_dec(b".\0".as_ptr(), ip[0] as u64);
            serial_write_dec(b".\0".as_ptr(), ip[1] as u64);
            serial_write_dec(b".\0".as_ptr(), ip[2] as u64);
            if has_gateway {
                serial_write_dec(b", gateway \0".as_ptr(), ip[3] as u64);
                serial_write_dec(b".\0".as_ptr(), gateway[0] as u64);
                serial_write_dec(b".\0".as_ptr(), gateway[1] as u64);
                serial_write_dec(b".\0".as_ptr(), gateway[2] as u64);
                serial_write_dec(b"\n\0".as_ptr(), gateway[3] as u64);
            } else {
                serial_write(b" (no gateway)\n\0".as_ptr());
            }
        }
        
        Self {
            device,
            interface: iface,
            sockets: SocketSet::new(vec![]),
            socket_map: BTreeMap::new(),
            next_fd: 1,
            gateway: if has_gateway { Some(gateway_addr) } else { None },
            dns_servers: vec![], // No DNS servers in static config
        }
    }
    
    pub fn poll(&mut self) {
        // Ensure interrupts are enabled for timer to work
        unsafe { core::arch::asm!("sti", options(nomem, nostack)); }
        
        // Strategy: Poll smoltcp repeatedly until no more work is done
        let mut total_processed = 0;
        let mut iterations = 0;
        let mut consecutive_idle = 0;
        const MAX_POLL_ITERATIONS: usize = 200; // Increased from 100
        const IDLE_THRESHOLD: usize = 3; // Reduced from 5 for faster exit
        
        loop {
            iterations += 1;
            if iterations > MAX_POLL_ITERATIONS {
                unsafe {
                    serial_write(b"[NETWORK] poll() hit iteration limit!\n\0".as_ptr());
                }
                break;
            }
            
            // CRITICAL: Update timestamp on EACH poll iteration (smoltcp uses this for timers!)
            let ticks = unsafe { timer_get_ticks() } as i64;
            let timestamp = Instant::from_millis(ticks * 10);
            
            let did_work = self.interface.poll(timestamp, &mut self.device, &mut self.sockets);
            if did_work {
                total_processed += 1;
                consecutive_idle = 0;
                unsafe {
                    serial_write(b"[NETWORK] poll iteration \0".as_ptr());
                    serial_write_dec(b" did work\n\0".as_ptr(), iterations as u64);
                }
            } else {
                consecutive_idle += 1;
                if consecutive_idle >= IDLE_THRESHOLD {
                    break;
                }
            }
        }
        
        // Debug: Show how many packets were processed
        if total_processed > 0 {
            unsafe { 
                serial_write(b"[NETWORK] poll() processed \0".as_ptr());
                serial_write_dec(b" packet(s) in \0".as_ptr(), total_processed as u64);
                serial_write_dec(b" iterations\n\0".as_ptr(), iterations as u64);
            }
        }
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
            serial_write(b"\n\0".as_ptr());
        }
        
        unsafe { serial_write(b"[PING] Creating ICMP socket buffers...\n\0".as_ptr()); }
        
        // Create ICMP socket
        let rx_buf = icmp::PacketBuffer::new(vec![icmp::PacketMetadata::EMPTY; 4], vec![0u8; 1024]);
        unsafe { serial_write(b"[PING] RX buffer created\n\0".as_ptr()); }
        
        let tx_buf = icmp::PacketBuffer::new(vec![icmp::PacketMetadata::EMPTY; 4], vec![0u8; 1024]);
        unsafe { serial_write(b"[PING] TX buffer created\n\0".as_ptr()); }
        
        let mut icmp_sock = icmp::Socket::new(rx_buf, tx_buf);
        unsafe { serial_write(b"[PING] Socket created\n\0".as_ptr()); }
        
        // Bind with an identifier; smoltcp permits Ident binding for echo matching
        icmp_sock.bind(icmp::Endpoint::Ident(0x1234)).ok();
        unsafe { serial_write(b"[PING] Socket bound\n\0".as_ptr()); }

        let handle = self.sockets.add(icmp_sock);
        unsafe { serial_write(b"[PING] Socket added to set\n\0".as_ptr()); }

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

        // Poll heavily initially to allow ARP resolution
        unsafe { serial_write(b"[PING] Polling for ARP resolution...\n\0".as_ptr()); }
        for i in 0..20 {
            self.poll();
            // Small yield every few polls to allow interrupts
            if i % 5 == 0 {
                for _ in 0..500 { core::hint::spin_loop(); }
            }
        }
        unsafe { serial_write(b"[PING] Initial polling complete\n\0".as_ptr()); }

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

            // (Re)send every ~200ms up to 5 times (increased from 3)
            if sent < 5 && now - last_tx_ms >= 200 {
                let socket = self.sockets.get_mut::<icmp::Socket>(handle);
                if socket.can_send() {
                    match socket.send_slice(&payload, dest) {
                        Ok(_) => {
                            unsafe { serial_write(b"[PING] sent #\0".as_ptr()); serial_write_dec(b"\0".as_ptr(), (sent + 1) as u64); }
                            last_tx_ms = now;
                            sent += 1;
                            
                            // CRITICAL: Poll immediately after send to actually transmit the packet
                            self.poll();
                        }
                        Err(e) => {
                            unsafe { serial_write(b"[PING] send failed, will retry\n\0".as_ptr()); }
                        }
                    }
                } else {
                    unsafe { serial_write(b"[PING] cannot send yet\n\0".as_ptr()); }
                }
            }

            // Poll multiple times per iteration for better responsiveness
            for _ in 0..5 {
                self.poll();
            }

            let socket = self.sockets.get_mut::<icmp::Socket>(handle);
            if socket.can_recv() {
                match socket.recv() {
                    Ok((data, from)) => {
                        unsafe { serial_write(b"[PING] recv len=\0".as_ptr()); serial_write_dec(b"\0".as_ptr(), data.len() as u64); }
                        if data.len() >= payload.len() {
                            rtt_ms = (now - start_ms) as i32;
                            unsafe { serial_write(b"[PING] SUCCESS! RTT=\0".as_ptr()); serial_write_dec(b"ms\n\0".as_ptr(), rtt_ms as u64); }
                            break;
                        }
                    }
                    Err(e) => {
                        // Continue polling
                    }
                }
            }
            
            // Small yield to allow timer interrupts
            if iterations % 10 == 0 {
                for _ in 0..1000 { core::hint::spin_loop(); }
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
            
            unsafe { 
                serial_write(b"[TCP] connect: attempting to connect to \0".as_ptr()); 
                serial_write_dec(b".\0".as_ptr(), ip[0] as u64);
                serial_write_dec(b".\0".as_ptr(), ip[1] as u64);
                serial_write_dec(b".\0".as_ptr(), ip[2] as u64);
                serial_write_dec(b":\0".as_ptr(), ip[3] as u64);
                serial_write_dec(b" from local port \0".as_ptr(), port as u64);
                serial_write_dec(b"\n\0".as_ptr(), local_port as u64);
            }
            
            match socket.connect(self.interface.context(), (remote_addr, port), local_port) {
                Ok(_) => {
                    unsafe { serial_write(b"[TCP] connect: socket.connect() succeeded\n\0".as_ptr()); }
                }
                Err(e) => {
                    unsafe { 
                        serial_write(b"[TCP] connect: socket.connect() failed with error\n\0".as_ptr()); 
                    }
                    return -1;
                }
            }
        }

        // Poll aggressively for ARP resolution and handshake completion
        unsafe { serial_write(b"[TCP] connect: polling for handshake completion...\n\0".as_ptr()); }
        
        let start_ms = (unsafe { timer_get_ticks() } as i64) * 10;
        let handshake_deadline = start_ms + 3000; // 3 seconds for handshake
        let mut poll_count = 0;
        
        loop {
            let now = (unsafe { timer_get_ticks() } as i64) * 10;
            if now >= handshake_deadline {
                unsafe { serial_write(b"[TCP] connect: handshake timeout\n\0".as_ptr()); }
                break;
            }
            
            // Poll the network stack - this processes incoming packets and sends responses
            self.poll();
            poll_count += 1;
            
            // Check socket state frequently
            let socket = self.sockets.get::<tcp::Socket>(handle);
            let state = socket.state();
            
            // Debug state every 50 polls
            if poll_count % 50 == 0 {
                let state_val = match state {
                    tcp::State::Closed => 0,
                    tcp::State::Listen => 1,
                    tcp::State::SynSent => 2,
                    tcp::State::SynReceived => 3,
                    tcp::State::Established => 4,
                    tcp::State::FinWait1 => 5,
                    tcp::State::FinWait2 => 6,
                    tcp::State::CloseWait => 7,
                    tcp::State::Closing => 8,
                    tcp::State::LastAck => 9,
                    tcp::State::TimeWait => 10,
                };
                unsafe {
                    serial_write(b"[TCP] connect: poll_count=\0".as_ptr());
                    serial_write_dec(b", state=\0".as_ptr(), poll_count as u64);
                    serial_write_dec(b", is_active=\0".as_ptr(), state_val as u64);
                    serial_write_dec(b"\n\0".as_ptr(), socket.is_active() as u64);
                }
            }
            
            // Check if connection is established
            if socket.is_active() && state == tcp::State::Established {
                unsafe {
                    serial_write(b"[TCP] connect: Connection established after \0".as_ptr());
                    serial_write_dec(b" polls!\n\0".as_ptr(), poll_count as u64);
                }
                return 0; // Success!
            }
            
            // Small yield every few polls to allow timer interrupts
            if poll_count % 10 == 0 {
                for _ in 0..500 { core::hint::spin_loop(); }
            }
        }
        
        // Final state check
        {
            let socket = self.sockets.get::<tcp::Socket>(handle);
            let state_val = match socket.state() {
                tcp::State::Closed => 0,
                tcp::State::Listen => 1,
                tcp::State::SynSent => 2,
                tcp::State::SynReceived => 3,
                tcp::State::Established => 4,
                tcp::State::FinWait1 => 5,
                tcp::State::FinWait2 => 6,
                tcp::State::CloseWait => 7,
                tcp::State::Closing => 8,
                tcp::State::LastAck => 9,
                tcp::State::TimeWait => 10,
            };
            unsafe {
                serial_write(b"[TCP] connect: Final state after \0".as_ptr());
                serial_write_dec(b" polls - state=\0".as_ptr(), poll_count as u64);
                serial_write_dec(b", is_active=\0".as_ptr(), state_val as u64);
                serial_write_dec(b"\n\0".as_ptr(), socket.is_active() as u64);
            }
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

        // Poll initially to drive connection establishment
        unsafe { serial_write(b"[TCP] send: polling for connection...\n\0".as_ptr()); }
        for _ in 0..100 {
            self.poll();
            if self.sockets.get::<tcp::Socket>(handle).is_active() {
                break;
            }
        }

        // Ensure socket is active before sending
        {
            let socket = self.sockets.get::<tcp::Socket>(handle);
            if !socket.is_active() {
                unsafe { 
                    serial_write(b"[TCP] send: socket not active after polling\n\0".as_ptr()); 
                }
                return -1;
            }
            unsafe { serial_write(b"[TCP] send: socket is active\n\0".as_ptr()); }
        }

        // Try to send with polling retries to handle WouldBlock
        let mut total_sent: usize = 0;
        let start_ms = (unsafe { timer_get_ticks() } as i64) * 10;
        let deadline = start_ms + 10000; // up to 10s overall (increased from 5s)
        let mut attempts = 0;
        loop {
            attempts += 1;
            let now = (unsafe { timer_get_ticks() } as i64) * 10;
            if now >= deadline { 
                unsafe { 
                    serial_write(b"[TCP] send: timeout after attempts=\0".as_ptr()); 
                    serial_write_dec(b"\0".as_ptr(), attempts as u64);
                    serial_write(b"\n\0".as_ptr());
                }
                break; 
            }

            {
                let socket = self.sockets.get_mut::<tcp::Socket>(handle);
                if !socket.is_active() {
                    unsafe { serial_write(b"[TCP] send: socket became inactive\n\0".as_ptr()); }
                    break;
                }
                if socket.may_send() && socket.can_send() {
                    match socket.send_slice(data) {
                        Ok(len) => { 
                            unsafe { 
                                serial_write(b"[TCP] send: success, len=\0".as_ptr()); 
                                serial_write_dec(b"\0".as_ptr(), len as u64);
                                serial_write(b"\n\0".as_ptr());
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

            // Drive the stack and try again - poll more frequently
            for _ in 0..5 {
                self.poll();
            }
            
            // Small yield to allow timer interrupts
            if attempts % 20 == 0 {
                for _ in 0..1000 { core::hint::spin_loop(); }
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

// Initialize network with custom IP configuration
#[no_mangle]
pub extern "C" fn network_init_with_ip(ip0: u8, ip1: u8, ip2: u8, ip3: u8, gw0: u8, gw1: u8, gw2: u8, gw3: u8) -> i32 {
    if unsafe { ACTIVE_DRIVER.is_some() } {
        let stack = NetworkStack::new_with_ip([ip0, ip1, ip2, ip3], [gw0, gw1, gw2, gw3]);
        *NETWORK_STACK.lock() = Some(stack);
        0
    } else {
        -1
    }
}

#[no_mangle]
pub extern "C" fn network_poll() {
    // Use try_lock to avoid deadlock if called from interrupt while stack is already locked
    if let Some(mut stack_guard) = NETWORK_STACK.try_lock() {
        if let Some(ref mut stack) = *stack_guard {
            stack.poll();
        }
    }
    // If we can't get the lock, just skip this poll - the next interrupt will try again
}

#[no_mangle]
pub extern "C" fn net_icmp_ping(ip: *const u8, timeout_ms: i32) -> i32 {
    unsafe { serial_write(b"[PING] net_icmp_ping called\n\0".as_ptr()); }
    if ip.is_null() { 
        unsafe { serial_write(b"[PING] ERROR: ip is null\n\0".as_ptr()); }
        return -1; 
    }
    
    unsafe { serial_write(b"[PING] Locking NETWORK_STACK...\n\0".as_ptr()); }
    let mut stack_lock = NETWORK_STACK.lock();
    unsafe { serial_write(b"[PING] Lock acquired\n\0".as_ptr()); }
    
    if let Some(ref mut stack) = *stack_lock {
        unsafe { serial_write(b"[PING] Stack exists, parsing IP...\n\0".as_ptr()); }
        let ip_slice = unsafe { core::slice::from_raw_parts(ip, 4) };
        let addr = [ip_slice[0], ip_slice[1], ip_slice[2], ip_slice[3]];
        unsafe { serial_write(b"[PING] Calling icmp_ping...\n\0".as_ptr()); }
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

// Get network configuration information
// Returns: 1 if configured, 0 if not configured, -1 on error
// Outputs: ip_out[4], netmask_out[4], gateway_out[4], mac_out[6]
#[no_mangle]
pub extern "C" fn net_get_config(
    ip_out: *mut u8,
    netmask_out: *mut u8,
    gateway_out: *mut u8,
    mac_out: *mut u8
) -> i32 {
    if let Some(ref stack) = *NETWORK_STACK.lock() {
        unsafe {
            // Get MAC address
            if !mac_out.is_null() {
                let mac = stack.device.mac();
                core::ptr::copy_nonoverlapping(mac.as_bytes().as_ptr(), mac_out, 6);
            }
            
            // Get IP address and netmask
            let ip_addrs = stack.interface.ip_addrs();
            if let Some(ip_cidr) = ip_addrs.iter().next() {
                if let IpAddress::Ipv4(ipv4) = ip_cidr.address() {
                    if !ip_out.is_null() {
                        let octets = ipv4.as_bytes();
                        core::ptr::copy_nonoverlapping(octets.as_ptr(), ip_out, 4);
                    }
                    
                    // Calculate netmask from prefix length
                    if !netmask_out.is_null() {
                        let prefix_len = ip_cidr.prefix_len();
                        let mask: u32 = if prefix_len == 0 {
                            0
                        } else {
                            !0u32 << (32 - prefix_len)
                        };
                        let mask_bytes = mask.to_be_bytes();
                        core::ptr::copy_nonoverlapping(mask_bytes.as_ptr(), netmask_out, 4);
                    }
                }
            } else {
                return 0; // Not configured
            }
            
            // Get default gateway
            if !gateway_out.is_null() {
                    // Get gateway from stored field
                    if let Some(gw) = stack.gateway {
                        let octets = gw.as_bytes();
                        core::ptr::copy_nonoverlapping(octets.as_ptr(), gateway_out, 4);
                    } else {
                        // No gateway configured
                        let zeros = [0u8; 4];
                        core::ptr::copy_nonoverlapping(zeros.as_ptr(), gateway_out, 4);
                }
            }
            
            return 1; // Configured
        }
    }
    -1 // Error - no network stack
}
