#!/usr/local/bin/env python

"""
Gale controls a common 3-speed fan using the readings from a Bluetooth HR monitor,
similar to Wahoo KICKR Headwind.
"""

from time import sleep
from machine import Pin
#from ubluetooth import BLE

# TODO: Can we read these from somewhere instead of hardcoding?
MAX_HR = 190
REST_HR = 40

# do not reduce the fan speed until HR is at least HR_HYSTERESIS BPM below the threshold
HR_HYSTERESIS = 5

# Fan speed relay pins
PIN_LOW = 25
PIN_MED = 26
PIN_HIGH = 27

class HeartRateMonitor:
    def __init__(self, max_hr, rest_hr):
        self.max_hr = max_hr
        self.rest_hr = rest_hr
        self.hr_reserve = max_hr - rest_hr

        # Switch the fan on at 30% of HR Reserve (e.g.: 85 BPM)
        self.threshold_low = (self.rest_hr + 0.3 * self.hr_reserve)

        # Switch to second speed at 75% of Max HR (e.g.: 142 BPM)
        self.threshold_med = 0.75 * self.max_hr

        # Switch to third speed at 90% of Max HR (e.g.: 171 BPM)
        self.threshold_high = 0.9 * self.max_hr

        self.prev_hr = 0
        self.hr = 0
        
    def discover(self):
        # TODO: Implement discovering a nearby HRM, perhaps with a button push
        pass

    def connect(self):
        # TODO: Connect to the HRM
        pass

    def read_hr(self):
        self.hr = 90 # TODO: read from HRM, I'm assuming for now it's function call, but it could be a callback
        return self.hr

    def calc_fan_speed(self, hr):
        self.speed = 0
        if hr > self.threshold_low or \
            (self.prev_hr >= hr and hr > self.threshold_low - HR_HYSTERESIS):
            self.speed = 1
        elif hr > self.threshold_med or \
            (self.prev_hr >= hr and hr > self.threeshold_med - HR_HYSTERESIS):
            self.speed = 2
        elif hr > self.threshold_high or \
            (self.prev_hr >= hr and hr > self.threeshold_high - HR_HYSTERESIS):
            self.speed = 3

        self.prev_hr = hr

        return self.speed


class FanSpeedController:
    def __init__(self, pin_low, pin_med, pin_high):
        self.led = Pin(2, Pin.OUT)
        self.pin_low = Pin(pin_low, Pin.OUT)
        self.pin_med = Pin(pin_med, Pin.OUT)
        self.pin_high = Pin(pin_high, Pin.OUT)

        self.speed = 0

    def set_fan_speed(self, speed):
        if speed == 0:
            self.pin_low.value(0)
            self.pin_med.value(0)
            self.pin_high.value(0)
            self.led.off()
        elif speed == 1:
            self.pin_low.value(1)
            self.pin_med.value(0)
            self.pin_high.value(0)
            self.led.on()
        elif speed == 2:
            self.pin_low.value(0)
            self.pin_med.value(1)
            self.pin_high.value(0)
            self.led.on()
        else:
            self.pin_low.value(0)
            self.pin_med.value(0)
            self.pin_high.value(1)
            self.led.on()

        self.speed = speed

    def get_speed(self):
        # TODO: read the current pin value
        return self.speed

def main():
    hr = HeartRateMonitor(MAX_HR, REST_HR)
    fan = FanSpeedController(PIN_LOW, PIN_MED, PIN_HIGH)

    while True:
        fan.set_fan_speed(hr.calc_fan_speed(hr.read_hr()))
        sleep(1)


if __name__ == "__main__":
    main()