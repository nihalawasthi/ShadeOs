#include <stdint.h>

// forward declarations (use the ones you already have)
extern void outb(uint16_t port, uint8_t val);
extern uint8_t inb(uint16_t port);

static inline uint8_t cmos_read(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}
static int bcd_to_bin(uint8_t val) {
    return (val & 0x0F) + ((val >> 4) * 10);
}

// Days in each month (non-leap year by default)
static const int days_in_month[12] = { 
    31,28,31,30,31,30,31,31,30,31,30,31 
};

static int is_leap_year(int year) {
    return (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}


void rtc_get_date(int *year, int *month, int *day, int *hour, int *minute, int *second) {
    *second = bcd_to_bin(cmos_read(0x00));
    *minute = bcd_to_bin(cmos_read(0x02));
    *hour   = bcd_to_bin(cmos_read(0x04));
    *day    = bcd_to_bin(cmos_read(0x07));
    *month  = bcd_to_bin(cmos_read(0x08));
    int y = bcd_to_bin(cmos_read(0x09));
    if (y < 50) {           // assume 2000–2049
        *year = 2000 + y;
    } else {                // assume 1950–1999
        *year = 1900 + y;
    }
    //hardcoded the timezone offset (IST = UTC + 5h30m)
     *minute += 30;
    if (*minute >= 60) {
        *minute -= 60;
        (*hour)++;
    }

    *hour += 5;
    if (*hour >= 24) {
        *hour -= 24;
        (*day)++;
        // (You can add month/day rollover logic if you want full accuracy)
    }

    // ✅ Handle month/day rollover
    int dim = days_in_month[*month - 1];
    if (*month == 2 && is_leap_year(*year)) dim = 29;

    if (*day > dim) {
        *day = 1;
        (*month)++;
        if (*month > 12) {
            *month = 1;
            (*year)++;
        }
    }
}
