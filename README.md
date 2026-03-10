# EPub-M5Stack-Paper-S3 (fork of EPub-InkPlate)

> **⚠️ Fork scope notice**: This repository is exclusively maintained for the **M5Stack Paper S3**. Inkplate device compatibility is **not a goal** and is not tested or preserved. If you need Inkplate support, use the upstream project instead.

This is a fork of [EPub-InkPlate](https://github.com/turgu1/EPub-InkPlate) by Guy Turcotte, ported and adapted to run on the **M5Stack Paper S3** (ESP32-S3, 8 MB PSRAM, 960×540 e-paper).

- **Original**: https://github.com/turgu1/EPub-InkPlate
- **Upstream**: https://github.com/juicecultus/EPub-M5Stack-Paper-S3
- **This fork**: https://github.com/jesusvallejo/EPub-M5Stack-Paper-S3

---

## What this fork changes

- **Target hardware**: M5Stack Paper S3 only (`paper_s3` environment in `platformio.ini`). All Inkplate-specific board targets still exist in the file but are not actively maintained.
- **Display driver**: Uses [epdiy](https://github.com/vroland/epdiy) for the Paper S3 EPD panel instead of the Inkplate library.
- **JPEG decoder**: Switched from TJPGD to [JPEGDEC](https://github.com/bitbank2/JPEGDEC) for the Paper S3 build, with a fix for progressive JPEG support on ESP32-S3 (see *Bug fixes* below).
- **ESP-IDF version**: Updated to 5.5.0 (upstream targets 4.x).

### Bug fixes applied in this fork

- **Progressive JPEG crash (EXCCAUSE=3 / LoadStoreError)**: EPubs containing progressive JPEG images (SOF2 marker) caused a boot loop on the Paper S3. Root cause: `CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY=y` places the 60 KB task stack in PSRAM. A stack-allocated `JPEGDEC` object put its internal `ucFileBuf` bit-stream buffer in PSRAM; JPEGDEC advances the buffer pointer by arbitrary byte offsets, producing unaligned 32-bit reads (`MOTOLONG`) that fault on the ESP32-S3 Xtensa LX7 when the target is PSRAM. Fixed in `src/models/jpeg_image.cpp` by allocating `JPEGDEC` in internal RAM via `heap_caps_malloc(MALLOC_CAP_INTERNAL)`.

---

## Quick start (M5Stack Paper S3)

```bash
git clone --recurse-submodules https://github.com/juicecultus/EPub-M5Stack-Paper-S3.git
cd EPub-M5Stack-Paper-S3

# Ensure submodules are initialized
git submodule update --init --recursive

# Build
pio run -e paper_s3

# Flash
pio run -e paper_s3 -t upload
```

The PlatformIO environment for this device is `paper_s3` (see `platformio.ini`).

## Last news (upstream)

(Updated 2022.5.01) — upstream version 2.0.1

- For Inkplate-6PLUS and Inkplate-10: The ESP-IDF-Inkplate library has been updated (v0.9.6) to support devices delivered without a second MCP chip onboard.
- For all Inkplates: Now using ESP-IDF framework v4.3.2.

## Known unresolved issue

[ ] A device reset may happen while reading a book and changing the current font as a background process computes page locations.

---

This is an EPub reader originally built for the e-Radionica Inkplate devices, adapted here for the M5Stack Paper S3.

Here are the main features (Paper S3 build):

- TTF and OTF embedded fonts support.
- Normal, Bold, Italic, Bold+Italic face types.
- Bitmap images display — baseline and progressive JPEG, PNG.
- EPub (V2, V3) book format subset.
- UTF-8 characters (supplied fonts limited to Latin-1).
- Screen orientation (portrait / landscape).
- Left, center, right, and justify text alignments.
- Font size and indentation.
- Limited CSS formatting.
- WiFi-based document download (web server).
- Table of contents.
- Multiple user-selectable fonts.
- Linear and matrix book list view.
- Real-time clock (BM8563).
- Keeps location of the last 10 books being read.

> Features that depend on Inkplate-specific hardware (tactile keys, Inkplate-6PLUS touch/backlit, battery management via Inkplate power IC) are not applicable to the Paper S3 build and may be stubbed or absent.

Upstream demo videos (Inkplate hardware, for reference only):

- The first working version of EPub-InkPlate [Here](https://www.youtube.com/watch?v=VnTLMhEgsqA).
- InkPlate-10 demonstration [Here](https://www.youtube.com/watch?v=qNAjbnEax8k).
- Inkplate-6PLUS demonstration [Here](https://www.youtube.com/watch?v=z1nvakbxiUQ).

Some pictures from the Linux version of the upstream project:

<img src="doc/pictures/books_select.png" alt="drawing" width="300"/><img src="doc/pictures/book_page.png" alt="drawing" width="300"/>

A picture of the Web Server in a browser:

<img src="doc/pictures/web_server.png" alt="drawing" width="500"/>

Books Directory List: Linear vs Matrix View:

<img src="doc/pictures/linear_view_6.png" alt="picture" width="300"/><img src="doc/pictures/matrix_view_6.png" alt="picture" width="300"/>


### Runtime environment

The EPub-InkPlate application requires that a micro-SD Card be present in the device. This micro-SD Card must be pre-formatted with a FAT32 partition. Two folders must be present in the partition: `fonts` and `books`. You must put the base fonts in the `fonts` folder and your EPub books in the `books` folder. The books must have the extension `.epub` in lowercase. 

Height font types are supplied with the application. For each type, there are four fonts supplied, to support regular, bold, oblique, and bold-italic glyphs. The application offers the user to select one of those font types as the default font. The fonts have been cleaned-up and contain only Latin-1 glyphs.

Another font is mandatory. It can be found in `SDCard/fonts/drawings.otf` and must also be located in the micro-SD Card `fonts` folder. It contains the icons presented in parameters/options menus.

The `SDCard` folder under GitHub reflects what the micro-SD Card should look like. One file is missing there is the `books_dir.db` that is managed by the application. It contains the meta-data required to display the list of available ebooks on the card and is automatically maintained by the application. It is refreshed at boot time and when the user requires it to do so through the parameters menu. The refresh process takes some time (between 5 and 10 seconds per ebook) but is required to get fast ebook directory list on screen.

### Fonts cleanup

All fonts have been subsetted to Latin-1 plus some usual characters. The `fonts/orig` folder in the GitHub project contains all original fonts that are processed using the script `fonts/subsetter.sh`. This script uses the Python **pyftsubset** tool that is part of the **fontTools** package. To install the tool, you need to execute the following command:

```bash
$ pip install fonttools brotli zopfli
```

The script takes all font from the `orig` folder and generate the new subset fonts in `subset-latin1/otf` folder. The following commands must be executed:

```bash
$ cd fonts
$ ./subsetter.sh
```

After that, all fonts in the `subset-latin1/otf` folder must be copied back in the `SDCard/fonts` folder.

## Development environment

[Visual Studio Code](https://code.visualstudio.com/) is the code editor I'm using. The [PlatformIO](https://platformio.org/) extension is used to manage application configuration for both Linux and the ESP32.

All source code is located in various folders:

- Source code shared across targets is in `include` and `src`
- Linux-only code is in `lib_linux`
- ESP32 / Paper S3 code is in `lib_esp32` (includes epdiy and EPub_InkPlate)
- The FreeType library for ESP32 is in `lib_freetype`
- Local copies of JPEGDEC and PNGdec libraries are in `JPEGDEC/` and `PNGdec/`

The file `platformio.ini` contains configuration for all environments. Only `paper_s3` is actively maintained in this fork.

Note that source code located in folders `old` and `test` is not used. It will be deleted from the project when the application development will be completed.

### Dependencies

The following are the libraries currently in use by the application:

- [GTK+3](https://www.gtk.org/) (Only for the Linux version) The development headers must be installed. This can be done with the following command (on Linux Mint):
  
  ``` bash
  $ sudo apt-get install build-essential libgtk-3-dev
  ```

The following are imported C header and source files, that implement some algorithms:

- [FreeType](https://www.freetype.org) (Parse, decode, and rasterize characters from TrueType fonts) A version of the library has been loaded in folder `freetype-2.10.4/` and compiled with specific options for the ESP32. See sub-section **FreeType library compilation for ESP32** below for further explanations.
- [PubiXML](https://pugixml.org/) (For XML parsing)
- [STB](https://github.com/nothings/stb) (For image resizing) :

  - `stb_image_resize.h` resize images larger/smaller 

- [PNGLE](https://github.com/kikuchan/pngle) (For PNG images) A modified version optimized for grayscale output instead of RGBA.
- [MINIZ](https://github.com/richgel999/miniz) (For ZIP/PNG/epub decompression)
- [TJPGD](http://elm-chan.org/fsw/tjpgd/00index.html) (For JPEG images — used on Inkplate builds)
- [JPEGDEC](https://github.com/bitbank2/JPEGDEC) (For JPEG images — used on the Paper S3 build, supports progressive JPEG)

The following libraries were used at first but replaced with counterparts:

- [ZLib](https://zlib.net/) deflating (unzip). A file deflation function is already supplied with `PNGLE`.
- [RapidXML](http://rapidxml.sourceforge.net/index.htm) (For XML parsing) Too much stack space required. Replaced with PubiXML.
- [SQLite3](https://www.sqlite.org/index.html) (The amalgamation version. For books simple database) Too many issues to get it runs on an ESP32. I built my own simple DB tool (look at `src/simple_db.cpp` and `include/simble_db.hpp`)
- [STB](https://github.com/nothings/stb) (For image extraction and unzip) Requires a lot of memory space depending on the ePub stored image resolution. Changed to use PNGLE and TJPGD combined with my own image classes to stream the image to the appropriate size without requiring to much memory space:

  - `stb_image.h` PNG and JPeg images extraction 

### FreeType library compilation for ESP32

The FreeType library is using a complex makefile structure to simplify (!) the compilation process. Here are the steps used to get a library suitable for integration in the EPub-InkPlate ESP32 application. As this process is already done, there is no need to run it again, unless a new version of the library is required or some changes to the modules selection are done.

1. The folder named `lib_freetype` is created to receive the library and its dependencies at install time:

    ``` bash
    $ mkdir lib_freetype
    ```

2. The ESP-IDF SDK must be installed in the main user folder. Usually, it is in folder ~/esp. The following location documents the installation procedure: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html . Look at Steps 1 to 4 (Setting Up Development Environment). This is important as the configuration setup below will access ESP32 related compilation tools.

3. The files `freetype-2.10.4/modules.cfg` and `freetype-2.10.4/include/freetype/config/ftoption.h` are modified to only keep the capabilities required to support TrueType and OpenType fonts. The original files have been saved in `*.orig` files.

4. A file named `freetype-2.10.4/myconf.sh` is created to simplify the configuration of the makefile structure. The `--prefix` option may require some modification to take into account the location where the EPub-InkPlate source code has been put. The `--prefix` must point to the `lib_freetype` folder.

5. The following commands are executed:

   ``` bash
   $ cd freetype-2.10.4
   $ bash myconf.sh
   $ make
   $ make install
   ```

   This will have created several files in the folder `lib_freetype`.

6. Edit file named `lib_freetype/lib/pkgconfig/freetype2.pc` and remove the entire line that contains `harfbuzz` reference.
7. Voilà...

### ESP-IDF configuration specifics

The EPub-InkPlate application requires some functionalities to be properly set up within the ESP-IDF. To do so, some parameters located in the `sdkconfig` file must be set accordingly. This must be done using the menuconfig application that is part of the ESP-IDF. 

The following is not required to be done as the file `sdkconfig.defaults` contains the changes that will trigger the generation of the suitable `sdkconfig.<project_name>` file related to the project being compiled.

The current release of PlatformIO allow for editing the `sdkconfig` through the PlatformIO's `Run Menuconfig` command located in the Project Tasks. 

The application will show a list of configuration aspects. 

The following elements have been done (No need to do it again as they are defined in file `sdkconfig.defaults`):
  
- **PSRAM memory management**: The PSRAM is an extension to the ESP32 memory that offers 4MB+4MB of additional RAM. The first 4MB is readily available to integrate into the dynamic memory allocation of the ESP-IDF SDK. To configure PSRAM:

  - Select `Component Config` > `ESP32-Specific` > `Support for external, SPI-Connected RAM`
  - Select `SPI RAM config` > `Initialize SPI RAM during startup`
  - Select `Run memory test on SPI RAM Initialization`
  - Select `Enable workaround for bug in SPI RAM cache for Rev 1 ESP32s`
  - Select `SPI RAM access method` > `Make RAM allocatable using malloc() as well`

  Leave the other options as they are. 

- **ESP32 processor speed**: The processor must be run at 240MHz. The following line in `platformio.ini` request this speed:

    ```
    board_build.f_cpu = 240000000L
    ```
  You can also select the speed in the sdkconfig file:

  - Select `Component config` > `ESP32-Specific` > `CPU frequency` > `240 Mhz`

- **FAT Filesystem Support**: The application requires the usage of the micro SD card. This card must be formatted on a computer (Linux or Windows) with a FAT32 partition (maybe not required as this is the default format of brand new cards). The following parameters must be adjusted in `sdkconfig`:

  - Select `Component config` > `FAT Filesystem support` > `Max Long filename length` > `255`
  - Select `Number of simultaneously open files protected  by lock function` > `5`
  - Select `Prefer external RAM when allocating FATFS buffer`
  - Depending on the language to be used (My own choice is Latin-1 (CP850)), select the appropriate Code Page for filenames. Select `Component config` > `FAT Filesystem support` > `OEM Code Page...`. DO NOT use Dynamic as it will add ~480KB to the application!!
  - Also select `Component config` > `FAT Filesystem support` > `API character encoding` > `... UTF-8 ...`

- **HTTP Server**: The application is supplying a Web server (through the use of HTTP) to the user to modify the list of books present on the SDCard. The following parameters must be adjusted:
  - Select `Component config` > `HTTP Server` > `Max HTTP Request Header Length` > 1024
  - Select `Component config` > `HTTP Server` > `Max HTTP URI Length` > 1024

- **WiFi memory buffers in PSRAM**: The WiFi implementation use a large portion of memory. There is not enough main memory available for the buffer required, so it must be allocated from the PSRAM:
  - Select `Component config` > `ESP32-specific` > `Support for externa,, SPI-connected RAM` > `SPI RAM config` > `Try to allocate memories of WiFi and LWIP in SPIRAM firstly.`

The following is not configured through *menuconfig:*

- **Flash memory partitioning**: the file `partitions.csv` contains the table of partitions required to support the application in the 4MB flash memory. The factory partition has been set to be ~2.4MB in size (OTA is not possible as the application is too large to accomodate this feature; the OTA related partitions have been commented out...). In the `platformio.ini` file, the line `board_build.partitions=...` is directing the use of these partitions configuration.
    
## In Memoriam

When I started this effort, I was aiming at supplying a tailored ebook reader for a friend of mine that has been impaired by a spinal cord injury for the last 13 years and a half. Reading books and looking at TV were the only activities she was able to do as she lost control of her body, from the neck down to the feet. After several years of physiotherapy, she was able to do some movement with her arms, without any control of her fingers. She was then able to push on buttons of an ebook reader with a lot of difficulties. I wanted to build a joystick-based interface to help her with any standard ebook reader but none of the commercially available readers allowed for this kind of integration.

On September 27th, 2020, we learned that she was diagnosed with the Covid-19 virus. She passed away during the night of October 1st.

I dedicate this effort to her. Claudette, my wife and I will always remember you!
