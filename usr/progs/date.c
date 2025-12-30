#include "../syscall.h"
#include "../string.h"

//leap year if divisible by 4, but if the year is divisble by 100, it is not a leap year unless divisble by 400
int isleap(int year) {
    int isleap = 0;
    if (year%4 == 0) {
        isleap = 1;
        if ((year%100 == 0) && (year%400 != 0)) {
            isleap = 0;
        }
    }
    return isleap;
}

void main(int argc, char* argv[]) {
    int fd = _open(-1, "dev/rtc0");
    if(fd < 0){
        dprintf(2, "Cannot open RTC\n");
        _exit();
    }
    unsigned long long time;
    if (_read(fd, &time, sizeof(time)) < 0) {
        dprintf(2, "Failed to read from RTC\n");
        _exit();
    }
    // nanoseconds to seconds???
    time = time/1000000000;
    
    //seconds since start of day
    unsigned long long seconds = time%(24*60*60);
    //clock value
    unsigned long long cur_hour = seconds/3600;
    unsigned long long cur_minute = (seconds%3600)/60;
    unsigned long long cur_second = seconds%3600%60;

    unsigned long long days = time/86400; //days since 1970 currently
    unsigned long long cur_year = 1970;

    // Walk forward from 1970, subtracting whole years, then whole months.
    while(days > 365){
        if (isleap(cur_year)) {
            days -= 366;
        } else {
            days -= 365;
        }
        cur_year++;
    }
    
    //leap year stuff
    int month_days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (isleap(cur_year)) { //leap year; feb+1
        month_days[1] = 29;
    }
        
    unsigned long long cur_month = 0;
    unsigned long long cur_day = days;

    for (; cur_month<12; cur_month++) {
        if (cur_day <= month_days[cur_month]) {
            break;
        }
        cur_day -= month_days[cur_month];
    }

    cur_day++; //++ cuz zero indexed

    // Whatever is left is the day of the month.
    
    //05 Dec 2025 18:00:00
    char * month_names[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", 
                             "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char * month_name = month_names[cur_month];

    char cur_day_buf[3];
    if (cur_day < 10) {
        snprintf(cur_day_buf, 3, "0%d", (int)cur_day);
    } else {
        snprintf(cur_day_buf, 3, "%d", (int)cur_day);
    }

    char cur_hour_buf[3];
    if (cur_hour < 10) {
        snprintf(cur_hour_buf, 3, "0%d", (int)cur_hour);
    } else {
        snprintf(cur_hour_buf, 3, "%d", (int)cur_hour);
    }

    char cur_min_buf[3];
    if (cur_minute < 10) {
        snprintf(cur_min_buf, 3, "0%d", (int)cur_minute);
    } else {
        snprintf(cur_min_buf, 3, "%d", (int)cur_minute);
    }

    char cur_sec_buf[3];
    if (cur_second < 10) {
        snprintf(cur_sec_buf, 3, "0%d", (int)cur_second);
    } else {
        snprintf(cur_sec_buf, 3, "%d", (int)cur_second);
    }
    
    dprintf(1, "%s %s %llu %s:%s:%s\n", cur_day_buf, month_name, cur_year, cur_hour_buf, cur_min_buf, cur_sec_buf);

    if(_close(fd) < 0){
        dprintf(2, "Could not close RTC\n");
        _exit();
    }

    _exit();
}