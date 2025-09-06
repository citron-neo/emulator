#!/bin/bash

FILE=build/bin/citron
if test -f "$FILE"; then
    # remove any previously made AppImage in the base citron git folder
    rm ./citron.AppImage

    # enter AppImage utility folder
    cd AppImageBuilder

    # run the build script to create the AppImage
    # (usage) ./build.sh [source citron build folder] [destination .AppImage file]
    ./build.sh ../build ./citron.AppImage

    FILE=./citron.AppImage
    if test -f "$FILE"; then
       # move the AppImage to the main citron folder
       mv citron.AppImage ..
       # return to main citron folder
       cd ..
       # show contents of current folder
       echo
       ls
       # show AppImages specifically
       echo
       ls *.AppImage
       echo
       echo "'citron.AppImage' is now located in the current folder."
       echo
    else
       cd ..
       echo "AppImage was not built."
    fi
else
    echo
    echo "$FILE does not exist."
    echo
    echo "No citron executable found in the /citron/build/bin folder!"
    echo
    echo "You must first build a native linux version of citron before running this script!"
    echo
fi
