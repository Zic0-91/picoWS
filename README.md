# picoWS
Pico Web Services

PicoWS is a pico web server that provides web services for your embedeed apps written in C.
This web server is very basic but it allow you to add web service to your apps.

### Build
##### Build PicoWS as a library
```sh
gcc  -O0 -g3 -Wall -c -fmessage-length=0 -fPIC picows.c
gcc -shared -o libpicows.so picows.o
```

##### Embeeded into your app
You can see exemple into PicoTU project
```sh
gcc -O0 -g3 -Wall -c -fmessage-length=0 -I picoWS/ picoTU/picoTU.c
gcc  -L"." -o picows picoTU.o -lpicows
```

### Usage
You can start the web server with port and resources like this :
```sh
LD_LIBRARY_PATH=. ./picows 9999 picoTU/Resources/
```

With your browser go to [http://localhost:9999](http://localhost:9999)



