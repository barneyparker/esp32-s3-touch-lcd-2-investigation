# Current State

In the /step-counter directory we have a project for the ESP32-S3 microcontroller built in Arduino format. The project is designed to connect to local wifi and send step count data to a server.

In the /stepper directory we have a test project. This has been used to test out additional functionality on another device, this time we have an LCD screen available.

In the /demo directory we have example projects in both Arduino and ESP-IDF formats. These are designed to be used as a starting point for new projects, and to demonstrate how to use the various libraries and tools that we have developed.

# Stepper Project

Our project should use the ESP-IDF framework, and be built using the ESP-IDF build system. We should use CMake to manage our build process. We will build the application both locally and in the cloud using GitHub Actions. We will use whatever tools are appropriate.

In the stepper directory we need to build an application which has the same functionality as the step-counter project, but also has the ability to display the step count on an LCD screen. This will allow us to test out the LCD screen and ensure that it is working correctly.

Our application stores wifi credentials in local storage. This allows us to connect to the wifi network without having to hardcode the credentials into the code. If we do not have credentials, or we are unable to connect to the any wifi networks we have details for, then we will start a wifi access point and web server to provide a captive portal for the user to enter their wifi credentials. Once we have the credentials, we will attempt to connect to the wifi network and send the step count data to the server.

Once connected to the wifi network we check a known url to see if a new firmware version is available. We keep a record of the current firmware version in local storage, and if we find a new version we will download it and install it. This allows us to easily update the firmware on the device without having to connect it to a computer.

Once connected to wifi, and after checking for firmware updates, we contact an NTP server to get the current time. This allows us to timestamp the step count data that we send to the server, and also allows us to display the current time on the LCD screen.

Once connected to wifi, after checking for firmware updates, and obtaining the correct time via NTP we use an ISR to capture "steps" from a stepper which has a magentic switch. Each time the switch is triggered, we increment a step count and send the updated count to the server. We also update the LCD screen with the new step count.

Steps may be bufered in local storage if we are unable to connect to the server, and we will attempt to resend the buffered steps each time we successfully connect to the wifi network.

If we open the wifi ap and captive portal, we should display a Wifi QR code to make connecting simple. Our screen is 320x240 and the QR code should be as large as is practical to make scanning easy. We can use a library like qrcode to generate the QR code, and then display it on the LCD screen.

The captive portal should be designed to be as simple as possible. It should be a mobile-first design. It should show currently visible wifi networks and allow the user to select the one they want to conenct to. It should then prompt the user for the wifi password, and provide a button to submit the credentials. Once the credentials are submitted, we should attempt to connect to the wifi network and provide feedback to the user on whether the connection was successful or not.

The device may store up to 10 wifi networks in local storage, and we should provide a way for the user to manage these networks. This could be done through the captive portal, where we can show a list of the stored networks and allow the user to delete any that they no longer want to use.

In "step mode" the device should display the current step count and the current time on the LCD screen. The step count should be updated each time a new step is detected, and the time should be updated every minute. The display should show:

- The current step count in large text, to make it easy to read at a glance.
- The current time in smaller text, to provide context for the step count.
- The size of the backlog of queued steps that have not yet been sent to the server, to provide feedback on whether the device is able to keep up with sending the data.
- The battery level, to provide feedback on the remaining battery life of the device.
- A Wifi indicator to show whether we have a current wifi connection or not (or we are in a "connecting" state)
- An idicator to show whther we are currently connected to the websocket or not, to provide feedback on whether the step count data is being sent to the server in real time or not.
