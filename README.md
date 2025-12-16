# BMD (Battery Management Division)
[![BMD Replacement Facility Video](https://img.youtube.com/vi/nqYUuZSCHp4/0.jpg)](https://www.youtube.com/watch?v=nqYUuZSCHp4)

## Project Main Idea
```
BMD (Battery Management Division) is a project 
that imagines a battery-future based possible dystopian.
```
In this distant future, batteries have evolved to a level that is far stronger, faster, safer, lighter, and smaller than anything we can imagine today. As a result, ultra-small and ultra-powerful batteries have become commercialized, and they can even be implanted inside the human body. At first, this technology creates an Utopian vision by helping humanity overcome disabilities (such as joint reinforcement or neural stimulation to help people recover from paralysis) and offering a better life. However, the government soon pushes citizens, whose productivity has increased thanks to these batteries, into an extreme competitive system. People live in a society where they continuously use and replace their body-implanted batteries under government control.

The department that manages this system is the BMD. It monitors every citizen’s battery level and usage patterns. To the government, the batteries are more important than the people, so citizens must always visit BMD facilities to replace their batteries. If they refuse, penalties such as restricted electronic device usage, blocked transaction, and suspended visas or passports are imposed. In this future, you must regularly visit the BMD and replace your battery to maintain a normal life and continue contributing your productivity to the government.

## Project Idea Building Process
```
This project began with a simple question:
“What kind of technology will play the most essential role in the future?”
```
I had heard that the development of mobile devices (AR glasses, compact VR headsets, high-performance multi-display smartphones, etc.) is often limited by battery technology. The high-speed and high-performance technologies already exist, but scaling them down into mobile form always depends on battery performance. Because of this, I strongly believe that the development of batteries is one of our most important future goals for technological progress.

The aim of this project is to explore a 'possible future.' To do that, I first needed to understand the history of batteries. Since the early 1800s, when the first battery was invented, battery technology has continuously evolved up to today. Because the past connects directly to the present, I am sure batteries will keep improving. Just as people 100 years ago could never imagine the batteries of 2025 (in fact, the battery inside one smartphone today might be stronger than all the batteries in the world 100 years ago combined), we can't predict how powerful batteries will be 100 years from now.

I believe that battery evolution will continue until we reach something close to an “ultimate battery.” Instead of imagining a magical, unrealistic battery that can do anything, I imagine something more grounded: a battery so small and powerful that it can enhance the human body. This “ultimate battery” creates a Utopia by overcoming physical limitations, enhancing assistive devices, and making advanced mobile technologies even more accessible. It replaces the need for sleep and food by supporting recovery and rest. Passionate people can chase their dreams by working harder and faster. Sports performance reaches new extremes, and society becomes obsessed. Eventually, this heightened productivity becomes the foundation of international competition.

The government sees how dramatically productivity increases through body-implanted batteries. If unsafe underground markets appear, or if citizens refuse to use their batteries, national growth would be seriously threatened. Especially because every country in the world is using batteries to boost productivity, falling behind is not an option. For this reason, the government establishes the BMD (similar to the DEA in our world but instead of controlling drugs it controls every citizen). The BMD monitors battery levels, usage tendency, and issues mandatory replacement orders.

All replacement procedures must take place inside official BMD replacement facilities. Unauthorized private replacements are considered invalid and result in penalties. Penalties apply whenever a citizen fails to follow the replacement order. The BMD can access all state-managed personal data and can block electronic devices, internet access, banking activity, visas, passports, and more. If a citizen ignores the orders for too long then later visits the facility, they receive a downgraded battery with reduced power and shorter usage time, forcing them to visit BMD more frequently. To recover, they must slowly rebuild their “score” by showing higher productivity.

This is the narrative contained in the current project. However, because the world is so interesting, I imagined further possibilities. Some citizens will definitely resist government control. They might escape to places beyond technological reach (such as caves) and form their own communities. These communities would no longer join the government’s extreme productivity system, instead living with primitive but functional technologies. From here, two futures might unfold:

1. The government strikes or controls these communities.
2. The communities grow, survive, and eventually become an independent nation that opposes the government and the BMD.

Whether the community wins or loses becomes another imagined future. Through the idea of the battery, we can explore many possible futures.

## Project Composition
### BMD Database (Webpage Side)
The BMD website monitors both your own battery status and the status of other users. Here, you can check annoucned replacement orders and any penalties applied to your account. A panel at the top shows your battery information, while the lower section shows the tendency trends of other users.

## BMD Replacement Facility (Hardware Installation)

This is the physical BMD replacement facility that replaces participant's batteries. When a participant receives a replacement order, they must tag their battery at the hardware to identify themselves. The participant places their battery-holding hand into an assigned area, and the machine automatically removes the old battery and installs a new one.

As a participatory artwork, the installation requires the following from all participants:

1. Submit personal data: name, date of birth, visa status, and activity tendency(Are you hardworker? Or not).
2. Wear the battery holder on their hand.
3. Continuously check their battery status through the BMD website.

Based on this information, the BMD system determines when the participant must replace their battery, and penalties are displayed on the website in real time.


## Project Aesthetic/Technology
### Website

The website is designed to feel like a government platform, clean, minimal, and clearly organized. Because the BMD site focuses on delivering information, I avoid unnecessary decoration.

The site is built with React. It uses LocalStorage to track previous visits and loads data that matches the user’s name. React was the best choice because it allows real-time updates based on other users’ data.

The database uses Firebase Firestore. Given the project scale, Firestore handles the data comfortably and makes it easy to detect changes in user data and display updates instantly.

### Hardware Installation

The installation uses two MCUs:

1. ESP32 LVGL 3.5-inch display
2. Arduino Nano

The ESP32 connects to the database via Wi-Fi and displays information on the touchscreen. Because the ESP32 LVGL board has limited digital pins, it communicates with the Arduino Nano through RX/TX.

The Arduino controls four MG90S micro servos, one large 20kg torque servo, and a PN532 NFC read/write module. Based on data sent from the ESP32, it performs the battery replacement sequence and writes updated user information onto the NFC-embedded battery.

I intentionally divided the hardware into multiple stages. Technically, a simple box with one servo would be enough to “swap” a battery, but I wanted participants to visually experience the replacement process. Watching their battery leave their hand and a new one attach reinforces the meaning of the project. It also recreates the feeling of visiting a government-controlled facility in the imagined future so that audiences in 2025 can feel that world.

---

## Future of This Project (What’s Next?)

There are two directions I want to focus on:

1. Making the penalties more real.
    
    Currently, penalties only appear as text on the website. They do not create any real inconvenience to the participant. To fully express the dystopian feature, the project needs a more physical or emotional sense of penalty. I feel the current version doesn't solve this issue enough.
    
2. Expanding the scale of the installation.
    
    The current hardware installation is miniature-sized. If the installation was larger, louder, and more mechanical, it would create a stronger sense of intimidation and better express the dystopian world. The limitation of scale is something I still need to improve.
3. Auto-setting the last battery replacement date
    
    Current Battery percentage is based on the 'Last Replacement Date' and 'Battery Due Date'. And 'Battery Due Date' is always depends on 'Last Replacement Date'. It means, experiences are coming from the Last Replacement Date, and currently, I'm asking participants to set it up manually. It has to be automatic for instant world diving.
---
---
---
# Protocol Process
## Webpage side
1. Participants submit their data thorugh BMD webpage
2. Connect BMD website with Firestore database
3. Render the infomations depends on the participant's name
4. Webpage will keep update the current states
5. If the participants receive the estimated battery replacement date, go to the BMD replacement facility(the hardware installation)
## Hardware side
1. Put the right hand(battery holder attached) on the hand area
2. Press the display panel
3. It reads the NFC tag info and indentify who the current participant is
4. If that participant needs to replace the battery, it proceed the replacement protocol
5. After the replacement, it automatically update participant's battery state on the database
---
---
---
# Circuit Diagram
![Diagram](https://i.ibb.co/PKjgDn4/BMD-circuit.png)


# MCU Setting
## ESP32 - 3.5 inch LVGL
### Board Info
https://www.lcdwiki.com/3.5inch_ESP32-32E_Display

### Library  Manager

1. Arduino_GFX_Library by moon on our nation
2. XPT2046_TouchScreen by Paul Stoffregen
3. U8g2 by oliver
4. Wifi.h
5. wifiClientSecure.h
6. HTTPClient.h
7. ArduinoJSON.h
8. time.h 
9. Adafruit_PN532.h

### Board Manager

1. ESP32 by Espressif Systems

### Arduino IDE upload setting

1. Tool -> Board -> 'ESP32 Dev Module'
2. Flash Size: 4mb
3. PSRAM: Disabled
4. Upload Speed: 115200 (921600 might break the upload)
5. Flash Mode: DIO
6. Flash Frequency: 40MHz
7. Partition Scheme: Default

[Press 'BOOT' on the board after press upload on IDE, like normal way to upload on ESP32]

### LVGL, Arduino GFX Library Font setting
[https://docs.rs/u8g2-fonts/latest/u8g2_fonts/fonts/index.html]

ex) gfx->setFont(u8g2_font_ncenB14_tr);


## Arduino Nano
### Board Info
https://store-usa.arduino.cc/products/arduino-nano?srsltid=AfmBOoph8_jaY7GA4x4YalveqywXhIOc4ZPMNN2wL61ehyn3vaXQgZqM
### Library Manager
1. servo.h

### Required components
1. MG90S micro servo *4
2. HD2012MG 20kg torque servo *1
3. PN532, NFC read/write module
4. DWEII 5V 2A Boost converter step-up power module
https://www.amazon.com/DWEII-Converter-Step-Up-Charging-Protection/dp/B09YD5C9QC/ref=sr_1_1?crid=1AQJNA309ITD6&dib=eyJ2IjoiMSJ9.GdNo_phsI2OuZGcp-_1fB8clrD2_0A4DXJOqlBal_xsrF-rlnfATXEFXYxool1x_GiZzn1N25lo8rz4Wn-l3brAk6V2FFbHjCr9huorcoZB1NHHgzixeXh2_jeI0l2SxpjxaRVcbLJoa-9fyyrDKP4rdCgkBw6aB8mas9F7oaokCva9pryr0SyG0PAsLd_XkgE6WIUevyF6O8haU4r1qNQMp5xCHS-nGCoQHaRLNqG8.NaaNpwTKzkVw3mE5SG-SfCtMk2DK4GvDurs2Vnj3W8w&dib_tag=se&keywords=deii%2Bcharging%2Bmodule&qid=1765661881&sprefix=dei%2Bcharging%2Bmodule%2Caps%2C114&sr=8-1&th=1
5. 3.7v Lithium Polymor battery *1
6. AA 1.5v battery *4

### API Key/Personal Info management
```
secret.h
```
Using 'secret.h' to manage every sensitive information.
Such as WIFI SSID and Password/Firebase API key.

