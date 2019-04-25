[![ubirch GmbH](files/cropped-uBirch_Logo.png)](https://ubirch.de)

# Low Power ESP32 Example

This example is based on [example-esp32](https://github.com/ubirch/example-esp32.git), but with deepsleep functionality.
For Further information, how to implement this example, please refer to the [example-esp32 README](https://github.com/ubirch/example-esp32/blob/master/README.md)

## Functional Description

The application is based on the RTC timer and the deep sleep functionality of
the ULP coprocessor. So, when the application processors wakes up, it starts
from the beginning of the application.

In order to know in which state the application is, this is stored in the
[system_status](https://github.com/ubirch/example-esp32-low-power/blob/master/main/main.c#L79)

After every wakeup the application first checks for the reason, why it woke up
in [get_wakeup_reason()](https://github.com/ubirch/example-esp32-low-power/blob/master/main/main.c#L132)
and afterwards checks, what it has to do in
[check_schedule()](https://github.com/ubirch/example-esp32-low-power/blob/master/main/main.c#L409).

The current implementation includes three schedulers, depending on the system time:

1. [sleep schedule](#sleep_schedule)
1. [ota schedule](#ota_schedule)
1. [sensor schedule](#sensor_schedule)



### [sleep_schedule](https://github.com/ubirch/example-esp32-low-power/blob/master/main/main.c#L390)

The sleep scheduler is entered, when the application controller wakes up and
there is nothing to do.
In this case the following tasks are created and executed:
- [sleep_task](https://github.com/ubirch/example-esp32-low-power/blob/master/main/main.c#L191)
Since the sleep task waits for the MAIN_SLEEP_READY bit, this is also set,
to allow the controller to go back to sleep.



### [ota_schedule](https://github.com/ubirch/example-esp32-low-power/blob/master/main/main.c#L398)

The Over The Air (OTA) scheduler is entered, when the application controller wakes up and
a over the air update has to be checked.
In this case the following tasks are created and executed:
- [wifi_task](https://github.com/ubirch/example-esp32-low-power/blob/master/main/main.c#L248)
this task connects to the wifi and sets the `NETWORK_STA_READY` bit,
if a wifi connection has been succesfully established.
- [ota_task](https://github.com/ubirch/example-esp32-low-power/blob/master/main/main.c#L335)
waits for an ethernet connection `NETWORK_ETH_READY` or a wifi connection `NETWORK_STA_READY`
and tries to update the firmware, if an update is provided.
When the task is completed, it stores the `next_ota_time` and sets the `MAIN_SLEEP_READY` bit.
- [sleep_task](https://github.com/ubirch/example-esp32-low-power/blob/master/main/main.c#L191)
waits for the `MAIN_SLEEP_READY` bit and allows the controller to go back to sleep.



### [sensor_schedule](https://github.com/ubirch/example-esp32-low-power/blob/master/main/main.c#L375)

The sensor scheduler is entered, when the application controller wakes up and
a sensor measurement has to be performed.
In this case the following tasks are created and executed:
- [wifi_task](https://github.com/ubirch/example-esp32-low-power/blob/master/main/main.c#L248)
this task connects to the wifi and sets the `NETWORK_STA_READY` bit,
if a wifi connection has been succesfully established.
- [update_time_task](https://github.com/ubirch/example-esp32-low-power/blob/master/main/main.c#L309)
waits for an ethernet connection `NETWORK_ETH_READY` or a wifi connection `NETWORK_STA_READY`
and tries to update the time via sntp.
When the task is completed, it sets the `MAIN_TIME_READY` bit.
- [key_register_task](https://github.com/ubirch/example-esp32-low-power/blob/master/main/main.c#L355)
waits for the `MAIN_TIME_READY` bit and performs a registers the public key at the backend,
if this is not already done.
- [sensor_task](https://github.com/ubirch/example-esp32-low-power/blob/master/main/main.c#L229)
waits for the `MAIN_TIME_READY` bit and performs a sensor measurement.
Afterwards it stores the `next_measurement_time` and sets the `MAIN_SEND_READY` flag.
- [send_data_task](https://github.com/ubirch/example-esp32-low-power/blob/master/main/main.c#L211)
waits for the `MAIN_SEND_READY` flag and transmits the measurement data to the backend.
Afterwards it sets the `MAIN_SLEEP_READY` bit.
- [sleep_task](https://github.com/ubirch/example-esp32-low-power/blob/master/main/main.c#L191)
waits for the `MAIN_SLEEP_READY` bit and allows the controller to go back to sleep.
- [enter_console_task](https://github.com/ubirch/example-esp32-low-power/blob/master/main/main.c#L278)
allows the user to enter the console to set the wifi connection,
or to check the status of the device. To enter the console, the user has to press `Ctrl+U` or `Ctrl+C`
on the keyboard, while the serial console is connected to the device.
This task disables all other tasks and also the watchdog timer, while it is running.
To exit the console task, the user has to enter `exit` and the application will resume.
**This part is still a bit buggy and has to be improved.**

