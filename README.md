# Extracting motion vectors from HEVC

Just run
`./build_and_run`


## Requirements
```
sudo apt install ffmpeg libsdl-dev libqt4-dev libqtgui4 libtool autotools-dev
git clone https://github.com/farindk/libvideogfx
cd libvideogfx
./autogen.sh
./configure
make -j8
sudo make install
```