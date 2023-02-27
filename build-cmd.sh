#!/usr/bin/env bash

# turn on verbose debugging output for parabuild logs.
exec 4>&1; export BASH_XTRACEFD=4; set -x
# make errors fatal
set -e
# complain about unset env variables
set -u

APR_INCLUDE_DIR="apr/include"

if [ -z "$AUTOBUILD" ] ; then
    exit 1
fi

if [ "$OSTYPE" = "cygwin" ] ; then
    autobuild="$(cygpath -u $AUTOBUILD)"
else
    autobuild="$AUTOBUILD"
fi

STAGING_DIR="$(pwd)"
TOP_DIR="$(dirname "$0")"

# load autobuild provided shell functions and variables
source_environment_tempfile="$STAGING_DIR/source_environment.sh"
"$autobuild" source_environment > "$source_environment_tempfile"
. "$source_environment_tempfile"

# Use msbuild.exe instead of devenv.com
build_sln() {
    local solution=$1
    local config=$2
    local proj="${3:-}"
    local toolset="${AUTOBUILD_WIN_VSTOOLSET:-v143}"

    # e.g. config = "Release|$AUTOBUILD_WIN_VSPLATFORM" per devenv.com convention
    local -a confparts
    IFS="|" read -ra confparts <<< "$config"

    msbuild.exe \
        "$(cygpath -w "$solution")" \
        ${proj:+-t:"$proj"} \
        -p:Configuration="${confparts[0]}" \
        -p:Platform="${confparts[1]}" \
        -p:PlatformToolset=$toolset
}

# extract APR version into VERSION.txt
APR_INCLUDE_DIR="../apr/include"
# will match -- #<whitespace>define<whitespace>APR_MAJOR_VERSION<whitespace>number  future proofed :)
major_version="$(sed -n -E 's/#[[:space:]]*define[[:space:]]+APR_MAJOR_VERSION[[:space:]]+([0-9]+)/\1/p' "${APR_INCLUDE_DIR}/apr_version.h")"
minor_version="$(sed -n -E 's/#[[:space:]]*define[[:space:]]+APR_MINOR_VERSION[[:space:]]+([0-9]+)/\1/p' "${APR_INCLUDE_DIR}/apr_version.h")"
patch_version="$(sed -n -E 's/#[[:space:]]*define[[:space:]]+APR_PATCH_VERSION[[:space:]]+([0-9]+)/\1/p' "${APR_INCLUDE_DIR}/apr_version.h")"
version="${major_version}.${minor_version}.${patch_version}"
build=${AUTOBUILD_BUILD_ID:=0}
echo "${version}.${build}" > "${STAGING_DIR}/VERSION.txt"

case "$AUTOBUILD_PLATFORM" in
  windows*)
    pushd "$TOP_DIR"
    RELEASE_OUT_DIR="$STAGING_DIR/lib/release"

    load_vsvars

    pushd "$STAGING_DIR"
    cmake -G "$AUTOBUILD_WIN_CMAKE_GEN" -A "$AUTOBUILD_WIN_VSPLATFORM" "$TOP_DIR/apr"
    for proj in apr-1 libapr-1
    do
        build_sln "APR.sln" "Release|$AUTOBUILD_WIN_VSPLATFORM" "$proj"
    done
    cmake -G "$AUTOBUILD_WIN_CMAKE_GEN" -A "$AUTOBUILD_WIN_VSPLATFORM" "$TOP_DIR/apr-util"
    for proj in aprutil libaprutil
    do
        build_sln "APR-Util.sln" "Release|$AUTOBUILD_WIN_VSPLATFORM" "$proj"
    done
    popd

    mkdir -p "$RELEASE_OUT_DIR" || echo "$RELEASE_OUT_DIR exists"

    if [ "$AUTOBUILD_ADDRSIZE" = 32 ]
      then 
        bitdir=""
      else
        bitdir="/x64"
    fi
    cp "apr$bitdir/LibR/apr-1.lib" "$RELEASE_OUT_DIR"
    cp "apr-util$bitdir/LibR/aprutil-1.lib" "$RELEASE_OUT_DIR"
    cp "apr-iconv$bitdir/LibR/apriconv-1.lib" "$RELEASE_OUT_DIR"
    cp "apr$bitdir/Release/libapr-1."{lib,dll} "$RELEASE_OUT_DIR"
    cp "apr-iconv$bitdir/Release/libapriconv-1."{lib,dll} "$RELEASE_OUT_DIR"
    cp "apr-util$bitdir/Release/libaprutil-1."{lib,dll} "$RELEASE_OUT_DIR"

    INCLUDE_DIR="$STAGING_DIR/include/apr-1"
    mkdir -p "$INCLUDE_DIR"      || echo "$INCLUDE_DIR exists"
    cp apr/include/*.h "$INCLUDE_DIR"
    cp apr-iconv/include/*.h "$INCLUDE_DIR"
    cp apr-util/include/*.h "$INCLUDE_DIR"
    mkdir "$INCLUDE_DIR/arch"    || echo "$INCLUDE_DIR/arch exists"
    cp apr/include/arch/apr_private_common.h "$INCLUDE_DIR/arch"
    cp -R "apr/include/arch/win32" "$INCLUDE_DIR/arch"
    mkdir "$INCLUDE_DIR/private" || echo "$INCLUDE_DIR/private exists"
    cp -R apr-util/include/private "$INCLUDE_DIR"
    popd
  ;;

  darwin*)
    PREFIX="$STAGING_DIR"

    opts="-arch $AUTOBUILD_CONFIGURE_ARCH $LL_BUILD_RELEASE"
    # per https://github.com/pyenv/pyenv/issues/1425
    export SDKROOT="/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk"
    export CC="clang"
    export CFLAGS="$opts -I$SDKROOT/usr/include"
    export CXXFLAGS="$opts"
    export LDFLAGS="$opts"
    export MAKEFLAGS="-j${AUTOBUILD_CPU_COUNT:-2}"

    pushd "$TOP_DIR/apr"
    ./configure --prefix="$PREFIX"
    make
    make install
    popd

    pushd "$TOP_DIR/apr-util"
    ./configure --prefix="$PREFIX" --with-apr="$PREFIX" --with-expat="$PREFIX"
    make
    make install
    popd

    # To conform with autobuild install-package conventions, we want to move
    # the libraries presently in "$PREFIX/lib" to "$PREFIX/lib/release".
    # We want something like:

    # libapr-1.a
    # libaprutil-1.a
    # libapr-1.0.dylib
    # libapr-1.dylib --> libapr-1.0.dylib
    # libaprutil-1.0.dylib
    # libaprutil-1.dylib --> libaprutil-1.0.dylib

    # But as of 2012-02-08, we observe that the real libraries are
    # libapr-1.0.4.5.dylib and libaprutil-1.0.4.1.dylib, with
    # libapr[util]-1.0.dylib (as well as libapr[util]-1.dylib) symlinked to
    # them. That's no good: our Copy3rdPartyLibs.cmake and viewer_manifest.py
    # scripts don't deal with the libapr[util]-1.0.major.minor.dylib files
    # directly, they want to manipulate only libapr[util]-1.0.dylib. Fix
    # things while relocating.

    mkdir -p "$PREFIX/lib/release" || echo "reusing $PREFIX/lib/release"
    for libname in libapr libaprutil
    do # First just move the static library, that part is easy
       mv "$PREFIX/lib/$libname-1.a" "$PREFIX/lib/release/"
       # Ensure that lib/release/$libname-1.0.dylib is a real file, not a symlink
       cp "$PREFIX/lib/$libname-1.0.dylib" "$PREFIX/lib/release"
       # Make sure it's stamped with the -id we need in our app bundle.
       # As of 2012-02-07, with APR 1.4.5, this function has been observed to
       # fail on TeamCity builds. Does the failure matter? Hopefully not...
       pushd "$PREFIX/lib/release"
       fix_dylib_id "$libname-1.0.dylib" || \
       echo "fix_dylib_id $libname-1.0.dylib failed, proceeding"
       popd
       # Recreate the $libname-1.dylib symlink, because the one in lib/ is
       # pointing to (e.g.) libapr-1.0.4.5.dylib -- no good
       ln -svf "$libname-1.0.dylib" "$PREFIX/lib/release/$libname-1.dylib"
       # Clean up whatever's left in $PREFIX/lib for this $libname (e.g.
       # libapr-1.0.4.5.dylib)
       rm "$PREFIX/lib/$libname-"*.dylib || echo "moved all $libname-*.dylib"
    done

    # When we linked apr-util against apr (above), it grabbed the -id baked
    # into libapr-1.0.dylib as of that moment. A libaprutil-1.0.dylib built
    # that way fails to load because it looks for
    # "$PREFIX/lib/libapr-1.0.dylib" even on the user's machine. We tried
    # horsing around with install_name_tool -id between building apr and
    # building apr-util, but that didn't work too well. Fix it after the fact
    # with install_name_tool -change.

    # <deep breath>

    # List library dependencies with otool -L. Skip the first two lines (tail
    # -n +3): the first is otool reporting which library file it's reading,
    # the second is that library's own -id stamp. Find embedded references to
    # our own build area (Bad). From each such line, isolate just the
    # pathname. (Theoretically we could use just awk instead of grep | awk,
    # but getting awk to deal with the forward-slashes embedded in the
    # pathname would be a royal pain. Simpler to use grep.) Now emit a -change
    # switch for each of those pathnames: extract the basename and change it
    # to the canonical relative Resources path. NOW: feed all those -change
    # switches into an install_name_tool command operating on that same
    # .dylib.
    lib="$PREFIX/lib/release/libaprutil-1.0.dylib"
    install_name_tool \
        $(otool -L "$lib" | tail -n +3 | \
          grep "$PREFIX/lib" | awk '{ print $1 }' | \
          (while read f; \
           do echo -change "$f" "@executable_path/../Resources/$(basename "$f")"; \
           done) ) \
        "$lib"
  ;;

  linux*)
    PREFIX="$STAGING_DIR"

    opts="-m$AUTOBUILD_ADDRSIZE $LL_BUILD_RELEASE"

    # do release builds
    pushd "$TOP_DIR/apr"
##      rm configure || echo "configure already gone"
##      autoreconf -fi
        LDFLAGS="$opts" CFLAGS="$opts" CXXFLAGS="$opts" \
            ./configure --prefix="$PREFIX" --libdir="$PREFIX/lib/release"
        make
        make install
    popd

    pushd "$TOP_DIR/apr-iconv"
##      rm configure || echo "configure already gone"
##      autoreconf -fi
        # NOTE: the autotools scripts in iconv don't honor the --libdir switch so we
        # need to build to a dummy prefix and copy the files into the correct place
        mkdir "$PREFIX/iconv"
        LDFLAGS="$opts" CFLAGS="$opts" CXXFLAGS="$opts" \
            ./configure --prefix="$PREFIX/iconv" --with-apr="../apr"
        make
        make install

        # move the files into place
        mkdir -p "$PREFIX/bin"
        cp -a "$PREFIX"/iconv/lib/* "$PREFIX/lib/release"
        cp -r "$PREFIX/iconv/include/apr-1" "$PREFIX/include/"
        cp "$PREFIX/iconv/bin/apriconv" "$PREFIX/bin/"
        rm -rf "$PREFIX/iconv"
    popd

    pushd "$TOP_DIR/apr-util"
##      rm configure || echo "configure already gone"
##      autoreconf -fi
        # the autotools can't find the expat static lib with the layout of our
        # libraries so we need to copy the file to the correct location temporarily
        cp "$PREFIX/packages/lib/release/libexpat.a" "$PREFIX/packages/lib/"

        # the autotools for apr-util don't honor the --libdir switch so we
        # need to build to a dummy prefix and copy the files into the correct place
        mkdir "$PREFIX/util"
        LDFLAGS="$opts" CFLAGS="$opts" CXXFLAGS="$opts" \
            ./configure --prefix="$PREFIX/util" \
            --with-apr="../apr" \
            --with-apr-iconv="../apr-iconv" \
            --with-expat="$PREFIX/packages/"
        make
        make install

        # move files into place
        mkdir -p "$PREFIX/bin"
        cp -a "$PREFIX"/util/lib/* "$PREFIX/lib/release/"
        cp -r "$PREFIX/util/include/apr-1" "$PREFIX/include/"
        cp "$PREFIX"/util/bin/* "$PREFIX/bin/"
        rm -rf "$PREFIX/util"
        rm -rf "$PREFIX/packages/lib/libexpat.a"
    popd

    # APR includes its own expat.h header that doesn't have all of the features
    # in the expat library that we have a dependency
    cp "$PREFIX/packages/include/expat/expat_external.h" "$PREFIX/include/apr-1/"
    cp "$PREFIX/packages/include/expat/expat.h" "$PREFIX/include/apr-1/"

    # clean
    pushd "$TOP_DIR/apr"
        make distclean
    popd
    pushd "$TOP_DIR/apr-iconv"
        make distclean
    popd
    pushd "$TOP_DIR/apr-util"
        make distclean
    popd
  ;;
esac

mkdir -p "$STAGING_DIR/LICENSES"
cat "$TOP_DIR/apr/LICENSE" > "$STAGING_DIR/LICENSES/apr_suite.txt"
