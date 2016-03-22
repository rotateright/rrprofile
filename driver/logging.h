#ifndef RRPROFILE_LOGGING_H
#define RRPROFILE_LOGGING_H

#include <linux/module.h>

#define MODULE_NAME "rrprofile"

extern int rrprofile_debug;

#define LOG_CALL() if(rrprofile_debug) { printk(KERN_INFO "[" MODULE_NAME "] %s()\n", __PRETTY_FUNCTION__); }

#define LOG_DEBUG(format, ...) if(rrprofile_debug) { printk(KERN_INFO "[" MODULE_NAME "] DEBUG: " format "\n", ## __VA_ARGS__); }

#define LOG_WARNING(format, ...) { printk(KERN_ALERT "[" MODULE_NAME "] WARNING: " format "\n", ## __VA_ARGS__); }

#define LOG_ERROR(format, ...) { printk(KERN_ALERT "[" MODULE_NAME "] ERROR: " format "\n", ## __VA_ARGS__); }

#endif // RRPROFILE_LOGGING_H
