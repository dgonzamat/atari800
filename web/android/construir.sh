#!/bin/bash
# Arma el APK a mano, sin Gradle: son una actividad y un fichero de assets,
# no hay dependencias que resolver y asi la compilacion no necesita red.
#
#   aapt2 compile  los recursos a .flat
#   aapt2 link     los junta con android.jar y saca el APK base mas R.java
#   javac + d8     la actividad a classes.dex
#   zipalign       alinea el APK (obligatorio antes de firmar)
#   apksigner      firma
#
# Uso:  construir.sh pagina.html [salida.apk]
#
# La clave de firma NO vive en el repo. Se toma de $FIRMA (por defecto
# ~/.atarixl/firma.keystore) y si no existe se crea una nueva. Ojo: Android
# solo deja instalar una actualizacion encima de la anterior si viene firmada
# con la MISMA clave, asi que esa clave hay que guardarla.
set -e

SDK=${ANDROID_HOME:-/opt/android-sdk}
# build-tools 35: el d8 de la 34 revienta con las clases internas que
# genera el javac 21 (NullPointerException dentro de R8)
BT=$SDK/build-tools/${BUILD_TOOLS:-35.0.0}
PLAT=$SDK/platforms/android-34/android.jar
AQUI=$(cd "$(dirname "$0")" && pwd)
PAGINA=${1:?falta la pagina (atari.html)}
SALIDA=${2:-$PWD/atari800xl.apk}
FIRMA=${FIRMA:-$HOME/.atarixl/firma.keystore}
OBRA=$(mktemp -d)
trap 'rm -rf "$OBRA"' EXIT

mkdir -p "$OBRA/flat" "$OBRA/gen" "$OBRA/clases" "$OBRA/assets"
cp "$PAGINA" "$OBRA/assets/atari.html"

echo "1/6 recursos"
"$BT/aapt2" compile --dir "$AQUI/res" -o "$OBRA/flat/res.zip"

echo "2/6 enlazado de recursos"
"$BT/aapt2" link -o "$OBRA/base.apk" \
    -I "$PLAT" \
    --manifest "$AQUI/AndroidManifest.xml" \
    -A "$OBRA/assets" \
    --java "$OBRA/gen" \
    --min-sdk-version 24 --target-sdk-version 34 \
    "$OBRA/flat/res.zip"

echo "3/6 java"
javac -source 8 -target 8 -nowarn -Xlint:-options \
    -bootclasspath "$PLAT" -classpath "$PLAT" -d "$OBRA/clases" \
    $(find "$AQUI/java" "$OBRA/gen" -name '*.java')

echo "4/6 dex"
"$BT/d8" --min-api 24 --lib "$PLAT" --output "$OBRA" \
    $(find "$OBRA/clases" -name '*.class')

echo "5/6 empaquetado"
cp "$OBRA/base.apk" "$OBRA/sin_alinear.apk"
(cd "$OBRA" && zip -q "$OBRA/sin_alinear.apk" classes.dex)
"$BT/zipalign" -f -p 4 "$OBRA/sin_alinear.apk" "$OBRA/alineado.apk"

echo "6/6 firma"
if [ ! -f "$FIRMA" ]; then
    echo "   no hay clave en $FIRMA: se crea una nueva"
    mkdir -p "$(dirname "$FIRMA")"
    keytool -genkeypair -keystore "$FIRMA" -storepass android -keypass android \
        -alias atarixl -keyalg RSA -keysize 2048 -validity 10950 \
        -dname "CN=Atari XL, OU=Emulador, O=DAGA SOFT, C=CL" >/dev/null 2>&1
fi
"$BT/apksigner" sign --ks "$FIRMA" --ks-pass pass:android --key-pass pass:android \
    --ks-key-alias atarixl --v1-signing-enabled true --v2-signing-enabled true \
    --out "$SALIDA" "$OBRA/alineado.apk"
"$BT/apksigner" verify --print-certs "$SALIDA" | grep -E 'DN|SHA-256' | head -2
ls -l "$SALIDA"
