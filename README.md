# Ersatz
![snazzy logo](logo.png)
A simple web browser that I am making for my A-level computing project.

## Build

First make sure you have a working C compiler and install headers for libCurl, libXML2 (AT LEAST 2.9.13), libSDL2, libSDL2-image, and libSDL2-ttf. Then run

```
make
```

## Running

Now you can run Ersatz by simply typing `./ersatz`.
Command-line options include:
```
--url=www.google.com # Set the starting URL
--fg=#123456         # Set the foreground colour
--bg=#123456         # Set the background colour
--hl=#123456         # Set the hyperlink colour
--sp=#123456         # Set the seperator colour
```

## Controls

Click the URL bar to enter a URL to navigate to. Use PgUp and PgDown to scroll up and down respectively. Hyperlinks are clickable as expected. The Back button, or backspace, will navigate to the previous page.
