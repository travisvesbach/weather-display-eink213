# Weather Display E-Ink 2.13

An application for the [Lilygo v2.3 2.13 inch ESP32 E-Ink board](https://github.com/Xinyuan-LilyGO/LilyGo-T5-Epaper-Series)

model: GxGDEW0213M21

resolution: 212x104

This application will query openweather api for current weather and forecast and display it on the e-ink screen.  Copy env.h.example to env.h and fill in the fields.

Original code based on [G6EJD/ESP32-e-Paper-Weather-Display example](https://github.com/G6EJD/ESP32-e-Paper-Weather-Display/tree/master/examples/Waveshare_2_13_T5) and heavily modified to fit my wants.

Icons are from [Meteocons](https://www.alessioatzeni.com/meteocons/) and converted using [image2cpp](https://javl.github.io/image2cpp/).  I did slightly modify a few of them to make them render better on the small e-ink display.

### Other libraries used
[ZinggJM/GxEPD2](https://github.com/ZinggJM/GxEPD2/blob/master/src/epd/GxEPD2_213_M21.h)
