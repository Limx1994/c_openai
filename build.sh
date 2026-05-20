#!/bin/bash
# Manual build script for c_openai project
# Uses MinGW gcc to compile libcurl (static) + openai_client + examples
set -e

# Compiler paths
CC="D:/APPS/Qt/Tools/mingw1120_64/bin/gcc.exe"
AR="D:/APPS/Qt/Tools/mingw1120_64/bin/ar.exe"

# Project root (use the junction link to avoid spaces in path)
ROOT="E:/c_openai"
BUILD="$ROOT/build"

# Output directories for object files
LIBCURL_OBJDIR="$BUILD/third_party/libcurl/lib"
OPENAI_OBJDIR="$BUILD/src/objs_openai"

# Create output directories
mkdir -p "$LIBCURL_OBJDIR"
mkdir -p "$OPENAI_OBJDIR"
mkdir -p "$BUILD/example"

# ============================================================
# STEP 1: Compile libcurl sources into static library
# ============================================================
echo "=== Step 1: Compiling libcurl ==="

LIBCURL_DEFS="-DBUILDING_LIBCURL -DCURL_HIDDEN_SYMBOLS -DCURL_STATICLIB -DHAVE_CONFIG_H"

LIBCURL_INCS="-I$ROOT/include \
-I$ROOT/third_party/libcurl/include \
-I$BUILD/third_party/libcurl/lib \
-I$ROOT/third_party/libcurl/lib"

LIBCURL_CFLAGS="-std=gnu11 \
-Werror-implicit-function-declaration -W -Wall -Wpedantic \
-Wbad-function-cast -Wconversion -Wmissing-declarations \
-Wmissing-prototypes -Wnested-externs -Wno-long-long -Wno-multichar \
-Wpointer-arith -Wshadow -Wsign-compare -Wundef -Wunused \
-Wwrite-strings -Waddress -Wattributes -Wcast-align -Wcast-qual \
-Wdeclaration-after-statement -Wdiv-by-zero -Wempty-body \
-Wendif-labels -Wfloat-equal -Wformat-security \
-Wignored-qualifiers -Wmissing-field-initializers -Wmissing-noreturn \
-Wno-padded -Wno-sign-conversion -Wno-switch-default -Wno-switch-enum \
-Wno-system-headers -Wold-style-definition -Wredundant-decls \
-Wstrict-prototypes -Wtype-limits -Wunreachable-code \
-Wunused-parameter -Wvla -Wclobbered -Wmissing-parameter-type \
-Wold-style-declaration -Wpragmas -Wstrict-aliasing=3 -ftree-vrp \
-Wjump-misses-init -Wno-pedantic-ms-format -Wdouble-promotion \
-Wformat=2 -Wtrampolines -Warray-bounds=2 -Wno-format-signedness \
-Wduplicated-cond -Wnull-dereference -fdelete-null-pointer-checks \
-Wshift-negative-value -Wshift-overflow=2 -Wunused-const-variable \
-Walloc-zero -Wduplicated-branches -Wformat-truncation=2 \
-Wimplicit-fallthrough -Wrestrict -Warith-conversion -Wenum-conversion \
-fvisibility=hidden"

# List of all libcurl source files
LIBCURL_SRCS="\
altsvc.c amigaos.c asyn-ares.c asyn-base.c asyn-thrdd.c \
bufq.c bufref.c cf-dns.c cf-h1-proxy.c cf-h2-proxy.c \
cf-haproxy.c cf-https-connect.c cf-ip-happy.c cf-socket.c cfilters.c \
conncache.c connect.c content_encoding.c cookie.c cshutdn.c \
curl_addrinfo.c curl_endian.c curl_fnmatch.c curl_fopen.c curl_get_line.c \
curl_gethostname.c curl_gssapi.c curl_memrchr.c curl_ntlm_core.c curl_range.c \
curl_sasl.c curl_sha512_256.c curl_share.c curl_sspi.c curl_threads.c \
curl_trc.c cw-out.c cw-pause.c dict.c dnscache.c \
doh.c dynhds.c easy.c easygetopt.c easyoptions.c \
escape.c fake_addrinfo.c file.c fileinfo.c formdata.c \
ftp.c ftplistparser.c getenv.c getinfo.c gopher.c \
hash.c headers.c hmac.c hostip.c hostip4.c \
hostip6.c hsts.c http.c http1.c http2.c \
http_aws_sigv4.c http_chunks.c http_digest.c http_negotiate.c http_ntlm.c \
http_proxy.c httpsrr.c idn.c if2ip.c imap.c \
ldap.c llist.c macos.c md4.c md5.c \
memdebug.c mime.c mprintf.c mqtt.c multi.c \
multi_ev.c multi_ntfy.c netrc.c noproxy.c openldap.c \
parsedate.c pingpong.c pop3.c progress.c protocol.c \
psl.c rand.c ratelimit.c request.c rtsp.c \
select.c sendf.c setopt.c sha256.c slist.c \
smb.c smtp.c socketpair.c socks.c socks_gssapi.c \
socks_sspi.c splay.c strcase.c strequal.c strerror.c \
system_win32.c telnet.c tftp.c thrdpool.c thrdqueue.c \
transfer.c uint-bset.c uint-hash.c uint-spbset.c uint-table.c \
url.c urlapi.c version.c ws.c"

LIBCURL_VAUTH_SRCS="\
vauth/cleartext.c vauth/cram.c vauth/digest.c vauth/digest_sspi.c \
vauth/gsasl.c vauth/krb5_gssapi.c vauth/krb5_sspi.c vauth/ntlm.c \
vauth/ntlm_sspi.c vauth/oauth2.c vauth/spnego_gssapi.c vauth/spnego_sspi.c \
vauth/vauth.c"

LIBCURL_VTLS_SRCS="\
vtls/apple.c vtls/cipher_suite.c vtls/gtls.c vtls/hostcheck.c \
vtls/keylog.c vtls/mbedtls.c vtls/openssl.c vtls/rustls.c \
vtls/schannel.c vtls/schannel_verify.c vtls/vtls.c vtls/vtls_scache.c \
vtls/vtls_spack.c vtls/wolfssl.c vtls/x509asn1.c"

LIBCURL_VQUIC_SRCS="\
vquic/curl_ngtcp2.c vquic/curl_quiche.c vquic/vquic.c vquic/vquic-tls.c"

LIBCURL_VSSH_SRCS="\
vssh/libssh.c vssh/libssh2.c vssh/vssh.c"

LIBCURL_CURLX_SRCS="\
curlx/base64.c curlx/basename.c curlx/dynbuf.c curlx/fopen.c \
curlx/inet_ntop.c curlx/inet_pton.c curlx/multibyte.c curlx/nonblock.c \
curlx/snprintf.c curlx/strcopy.c curlx/strdup.c curlx/strerr.c \
curlx/strparse.c curlx/timediff.c curlx/timeval.c curlx/version_win32.c \
curlx/wait.c curlx/warnless.c curlx/winapi.c"

ALL_LIBCURL_SRCS="$LIBCURL_SRCS $LIBCURL_VAUTH_SRCS $LIBCURL_VTLS_SRCS $LIBCURL_VQUIC_SRCS $LIBCURL_VSSH_SRCS $LIBCURL_CURLX_SRCS"

LIBCURL_OBJS=""
COUNT=0
TOTAL=$(echo $ALL_LIBCURL_SRCS | wc -w)

for src in $ALL_LIBCURL_SRCS; do
    COUNT=$((COUNT + 1))
    basename=$(echo "$src" | sed 's|/|_|g' | sed 's|\.c$|.o|')
    objpath="$LIBCURL_OBJDIR/$basename"
    srcpath="$ROOT/third_party/libcurl/lib/$src"

    if [ ! -f "$srcpath" ]; then
        echo "  [$COUNT/$TOTAL] SKIP (not found): $src"
        continue
    fi

    echo "  [$COUNT/$TOTAL] Compiling $src"
    "$CC" $LIBCURL_DEFS $LIBCURL_INCS $LIBCURL_CFLAGS -c "$srcpath" -o "$objpath"
    LIBCURL_OBJS="$LIBCURL_OBJS $objpath"
done

# Create static library
LIBCURL_A="$LIBCURL_OBJDIR/libcurl.a"
echo "  Creating static library: $LIBCURL_A"
rm -f "$LIBCURL_A"
"$AR" rc "$LIBCURL_A" $LIBCURL_OBJS
echo "  libcurl.a created successfully."

# ============================================================
# STEP 2: Compile openai_client sources
# ============================================================
echo ""
echo "=== Step 2: Compiling openai_client ==="

OPENAI_DEFS="-DCURL_STATICLIB"
OPENAI_INCS="-I$ROOT/include -I$ROOT/third_party/libcurl/include"
OPENAI_CFLAGS="-std=gnu11 -Wall -Wextra"

OPENAI_SRCS="openai_client.c openai_json.c openai_http_curl.c"

OPENAI_OBJS=""
for src in $OPENAI_SRCS; do
    objname=$(echo "$src" | sed 's|\.c$|.o|')
    objpath="$OPENAI_OBJDIR/$objname"
    srcpath="$ROOT/src/$src"

    echo "  Compiling $src"
    "$CC" $OPENAI_DEFS $OPENAI_INCS $OPENAI_CFLAGS -c "$srcpath" -o "$objpath"
    OPENAI_OBJS="$OPENAI_OBJS $objpath"
done

# ============================================================
# STEP 3: Link example executables
# ============================================================
echo ""
echo "=== Step 3: Linking example executables ==="

EXAMPLE_INCS="-I$ROOT/include"

# System libraries needed for static linking on MinGW/Windows
SYSTEM_LIBS="-lws2_32 -lwldap32 -lbcrypt -lcrypt32 -ladvapi32 -lkernel32 -luser32 -lgdi32 -lnormaliz -liphlpapi -lsecur32"

# Link chat_example
echo "  Linking chat_example.exe"
"$CC" -std=gnu11 $EXAMPLE_INCS \
    "$ROOT/example/chat_example.c" \
    $OPENAI_OBJS \
    "$LIBCURL_A" \
    $SYSTEM_LIBS \
    -o "$BUILD/example/chat_example.exe"

# Link anthropic_example
echo "  Linking anthropic_example.exe"
"$CC" -std=gnu11 $EXAMPLE_INCS \
    "$ROOT/example/anthropic_example.c" \
    $OPENAI_OBJS \
    "$LIBCURL_A" \
    $SYSTEM_LIBS \
    -o "$BUILD/example/anthropic_example.exe"

echo ""
echo "=== Build complete ==="
echo "  Executables:"
echo "    $BUILD/example/chat_example.exe"
echo "    $BUILD/example/anthropic_example.exe"
