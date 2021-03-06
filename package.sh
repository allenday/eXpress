#!/bin/bash

VERSION=1.5.1
OLD_VERSION=1.5.0
MAC=express-$VERSION-macosx_x86_64
LINUX=express-$VERSION-linux_x86_64
WIN=express-$VERSION-win32_x86_64
SRC=express-$VERSION-src

#make version update check file file
echo \$$VERSION\$ > html/curr_xprs_version

#Update documentation
rm -r html/doc
/Applications/Doxygen.app/Contents/Resources/doxygen Doxyfile
#Add logo to manual
awk 'NR==43{print "\\includegraphics{../../img/logo.png}\\\\"}1' html/doc/latex/refman.tex > html/doc/latex/temp.tex
awk 'NR==45{print "\\url{http://bio.math.berkeley.edu/eXpress}\\\\"}1' html/doc/latex/temp.tex > html/doc/latex/refman.tex
make -C html/doc/latex
mv html/doc/html/* html/doc
rmdir html/doc/html
mv html/doc/latex/refman.pdf html/doc/express-doc.pdf
rm -r html/doc/latex

sed -i '' "s/express-$OLD_VERSION/express-$VERSION/g" html/*.html

#Create new download directories
mkdir html/downloads/express-$VERSION
mkdir html/downloads/express-$VERSION/$MAC
mkdir html/downloads/express-$VERSION/$LINUX
mkdir html/downloads/express-$VERSION/$WIN
mkdir html/downloads/express-$VERSION/$SRC

#Populate download directories
#macosx
cp osx_build/src/express html/downloads/express-$VERSION/$MAC
cp README html/downloads/express-$VERSION/$MAC/README
cp LICENSE html/downloads/express-$VERSION/$MAC/LICENSE
cp -r sample_data html/downloads/express-$VERSION/$MAC/sample_data
#linux
cp linux_build/src/express html/downloads/express-$VERSION/$LINUX
cp README html/downloads/express-$VERSION/$LINUX/README
cp LICENSE html/downloads/express-$VERSION/$LINUX/LICENSE
cp -r sample_data html/downloads/express-$VERSION/$LINUX/sample_data
#windows
cp win_build/src/express.exe html/downloads/express-$VERSION/$WIN
cp win_build/src/zlibd1.dll html/downloads/express-$VERSION/$WIN
cp README html/downloads/express-$VERSION/$WIN/README
cp LICENSE html/downloads/express-$VERSION/$WIN/LICENSE
cp -r sample_data html/downloads/express-$VERSION/$WIN/sample_data
#src
cp -r src html/downloads/express-$VERSION/$SRC
cp README html/downloads/express-$VERSION/$SRC/README
cp LICENSE html/downloads/express-$VERSION/$SRC/LICENSE
cp -r sample_data html/downloads/express-$VERSION/$SRC/sample_data
cp html/doc/express-doc.pdf html/downloads/express-$VERSION/$SRC
cp CMakeLists.txt html/downloads/express-$VERSION/$SRC/CMakeLists.txt

find . -name '*.DS_Store' -type f -delete

#Tar download directories
cd html/downloads/express-$VERSION
tar -czf $MAC.tgz $MAC 
tar -czf $LINUX.tgz $LINUX
tar -czf $SRC.tgz $SRC
zip -mr $WIN.zip $WIN
rm -r $MAC
rm -r $LINUX
rm -r $SRC

#add new files to git
git add .

#return to start directory
cd ../../../


