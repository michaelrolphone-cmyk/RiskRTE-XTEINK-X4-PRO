#!/usr/bin/env python3
"""Conventional ordinary-package builder for the optional X4 RTC provider."""
from build_x4pro_drivers import build_one
if __name__ == '__main__':
    build_one('x4pro_rtc')
