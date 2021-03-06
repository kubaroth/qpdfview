# Qpdfview
Qpdfview is a tabbed document viewer using Poppler, libspectre, DjVuLibre, CUPS and Qt, licensed under GPL version 2 or later.

The project homepage is "https://launchpad.net/qpdfview". The project maintainer is "Adam Reichold <adam.reichold@t-online.de>".

# Dependencies
It depends on libQtCore, libQtGui. It also depends on libQtSvg, libQtSql, libQtDBus, libcups, resp. libz if SVG, SQL, D-Bus, CUPS, resp. SyncTeX support is enabled. It also depends on libmagic if Qt version 4 is used and libmagic support is enabled. The PDF plug-in depends on libQtCore, libQtXml, libQtGui and libpoppler-qt4 or libpoppler-qt5. The PS plug-in depends on libQtCore, libQtGui and libspectre. The DjVu plug-in depends on libQtCore, libQtGui and libdjvulibre. The Fitz plug-in depends on libQtCore, libQtGui and libmupdf.

# Build options
It is built using "lrelease qpdfview.pro", "qmake qpdfview.pro" and "make". It is installed using "make install". The installation paths are defined in "qpdfview.pri".

The following build-time options are available:
- 'without_svg' disables SVG support, i.e. fallback and application-specific icons will not be available.
- 'without_sql' disables SQL support, i.e. restoring tabs, bookmarks and per-file settings will not be available.
- 'without_dbus' disables D-Bus support, i.e. the '--unique' command-line option will not be available.
- 'without_pkgconfig' disables the use of pkg-config, i.e. compiler and linker options have to be configured manually in "qpdfview.pri".
- 'without_pdf' disables PDF support, i.e. the PDF plug-in using Poppler will not be built.
- 'without_ps' disables PS support, i.e. the PS plug-in using libspectre will not be built.
- 'without_djvu' disables DjVu support, i.e. the DjVu plug-in using DjVuLibre will not be built.
- 'with_fitz' enables Fitz support, i.e. the Fitz plug-in using MuPDF will be built.
- 'static_pdf_plugin' links the PDF plug-in statically (This could lead to linker dependency collisions.)
- 'static_ps_plugin' links the PS plug-in statically. (This could lead to linker dependency collisions.)
- 'static_djvu_plugin' links the DjVu plug-in statically. (This could lead to linker dependency collisions.)
- 'static_fitz_plugin' links the Fitz plug-in statically. (This could lead to linker dependency collisions.)
- 'without_cups' disables CUPS support, i.e. the program will attempt to rasterize the document instead of requesting CUPS to print the document file.
- 'without_synctex' disables SyncTeX support, i.e. the program will not perform forward and inverse search for sources.
- 'without_magic' disables libmagic support, i.e. the program will determine file type using the file suffix.
- 'without_signals' disabled support for UNIX signals, i.e. the program will not save bookmarks, tabs and per-file settings on receiving SIGINT or SIGTERM.

For example, if one wants to build the program without support for CUPS and PostScript, one could run "qmake CONFIG+="without_cups without_ps" qpdfview.pro" instead of "qmake qpdfview.pro".

The fallback and application-specific icons are derived from the Tango icon theme available at "http://tango.freedesktop.org".


## Building on Arm (Raspberry Pi)

### Dependencies

#### Openjpeg

https://github.com/uclouvain/openjpeg.git

```
git clone https://github.com/uclouvain/openjpeg.git
mkdir build; cd build; cmake ..;
make
sudo make install
```

#### Poppler

https://anongit.freedesktop.org/git/poppler/poppler.git

```
git clone https://anongit.freedesktop.org/git/poppler/poppler.git

mkdir build; cd build;
cmake .. -DCMAKE_INSTALL_PREFIX=$HOME/toolchains/qt512/  -DCMAKE_BUILD_TYPE=release -DWITH_TIFF=OFF -DWITH_NSS3=OFF -DTESTDATADIR=/home/pi/SRC/poppler/test
make install 
```

#### qtpdfview - quick start

https://github.com/bendikro/qpdfview.git

In qpdfview.pri, set qt toolchain root location. (I tend to keep different versions of qt in non-default location ($HOME/toolchains/qt512...etc) 
To get started with the minimum set of dependencies, just keep pdf, and svg and exclude cups, djvu and ps.
Also (in particular for Arm) I tend to skip using pkgconfig.

```
QT_ROOT = $$(HOME)/toolchains/qt512/5.12.0/gcc_64

mkdir build; cd build;
qmake -r CONFIG+=without_pkgconfig CONFIG+=without_cups CONFIG+=without_ps CONFIG+=without_djvu ../qpdfview.pro

make
```
