package com.dgonzamat.atarixl;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.util.Base64;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.webkit.JavascriptInterface;
import android.webkit.ValueCallback;
import android.webkit.WebChromeClient;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Toast;
import java.io.OutputStream;

/* La aplicacion entera es la pagina: esta actividad solo la abre y le da lo
   que un navegador no puede, que es esconder las barras del telefono al
   jugar a pantalla completa, saber que hacer con el boton de Atras y
   traer y llevar archivos: un WebView no abre el selector de archivos ni
   descarga nada por su cuenta, y el BASIC sube y baja programas. */
public class MainActivity extends Activity {

    private static final int ABRIR = 1, GUARDAR = 2;

    private WebView web;
    private ValueCallback<Uri[]> alElegir;   /* quien espera el archivo a subir */
    private byte[] porGuardar;               /* lo que espera destino para bajar */

    @Override
    protected void onCreate(Bundle guardado) {
        super.onCreate(guardado);
        web = new WebView(this);
        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        /* la maquina elegida se recuerda en localStorage */
        s.setDomStorageEnabled(true);
        s.setAllowFileAccess(true);
        /* el sonido lo arranca igual un toque en la pantalla: la pagina lo
           pide asi porque es lo que exigen los navegadores */
        s.setMediaPlaybackRequiresUserGesture(true);
        web.setWebViewClient(new WebViewClient());
        /* Subir un programa: el <input type="file"> de la pagina abre el
           selector del telefono. Se piden todos los tipos porque .BAS y .LST
           no tienen tipo registrado y filtrando por ellos no saldria nada. */
        web.setWebChromeClient(new WebChromeClient() {
            @Override
            public boolean onShowFileChooser(WebView v, ValueCallback<Uri[]> cb,
                                             FileChooserParams p) {
                if (alElegir != null) alElegir.onReceiveValue(null);
                alElegir = cb;
                Intent i = new Intent(Intent.ACTION_GET_CONTENT);
                i.addCategory(Intent.CATEGORY_OPENABLE);
                i.setType("*/*");
                try {
                    startActivityForResult(Intent.createChooser(i, "Programa para el Atari"), ABRIR);
                } catch (Exception e) {
                    alElegir = null;
                    return false;
                }
                return true;
            }
        });
        web.addJavascriptInterface(new Puente(), "AndroidXL");
        setContentView(web);
        if (guardado != null) web.restoreState(guardado);
        else web.loadUrl("file:///android_asset/atari.html");
    }

    /* Lo que la pagina puede pedirle al telefono. */
    private class Puente {
        @JavascriptInterface
        public void pantallaCompleta(final boolean si) {
            runOnUiThread(new Runnable() {
                @Override public void run() { barras(!si); }
            });
        }

        /* Bajar un programa: el telefono pregunta donde dejarlo (el mismo
           dialogo de "Guardar como" del sistema) y ahi se escribe. */
        @JavascriptInterface
        public void guardar(final String nombre, final String base64) {
            runOnUiThread(new Runnable() {
                @Override public void run() {
                    porGuardar = Base64.decode(base64, Base64.DEFAULT);
                    Intent i = new Intent(Intent.ACTION_CREATE_DOCUMENT);
                    i.addCategory(Intent.CATEGORY_OPENABLE);
                    i.setType(nombre.toLowerCase().endsWith(".txt") ? "text/plain"
                                                                      : "application/octet-stream");
                    i.putExtra(Intent.EXTRA_TITLE, nombre);
                    try { startActivityForResult(i, GUARDAR); }
                    catch (Exception e) { porGuardar = null; aviso("No hay donde guardarlo"); }
                }
            });
        }
    }

    @Override
    protected void onActivityResult(int pedido, int resultado, Intent datos) {
        super.onActivityResult(pedido, resultado, datos);
        Uri uri = (resultado == RESULT_OK && datos != null) ? datos.getData() : null;
        if (pedido == ABRIR) {
            if (alElegir != null) alElegir.onReceiveValue(uri != null ? new Uri[] { uri } : null);
            alElegir = null;
        } else if (pedido == GUARDAR) {
            byte[] b = porGuardar;
            porGuardar = null;
            if (uri == null || b == null) return;
            try {
                OutputStream o = getContentResolver().openOutputStream(uri);
                o.write(b);
                o.close();
                aviso("Guardado");
            } catch (Exception e) {
                aviso("No se pudo guardar");
            }
        }
    }

    private void aviso(String t) { Toast.makeText(this, t, Toast.LENGTH_SHORT).show(); }

    /* A pantalla completa se van las barras de estado y de navegacion, y la
       pantalla no se apaga sola en mitad de una partida; al salir, vuelve
       todo como estaba. */
    private void barras(boolean ver) {
        if (ver) getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        else getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if (Build.VERSION.SDK_INT >= 30) {
            WindowInsetsController c = getWindow().getInsetsController();
            if (c == null) return;
            if (ver) {
                c.show(WindowInsets.Type.systemBars());
            } else {
                c.hide(WindowInsets.Type.systemBars());
                c.setSystemBarsBehavior(
                    WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            int f = ver ? View.SYSTEM_UI_FLAG_VISIBLE
                        : View.SYSTEM_UI_FLAG_FULLSCREEN
                          | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                          | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                          | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                          | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                          | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN;
            getWindow().getDecorView().setSystemUiVisibility(f);
        }
    }

    /* Atras primero saca de pantalla completa; si no se estaba jugando asi,
       cierra la aplicacion como cualquier otra. */
    @Override
    public void onBackPressed() {
        web.evaluateJavascript(
            "window.salirPantallaCompleta ? window.salirPantallaCompleta() : false",
            new ValueCallback<String>() {
                @Override public void onReceiveValue(String v) {
                    if (!"true".equals(v)) MainActivity.super.onBackPressed();
                }
            });
    }

    @Override protected void onPause()  { web.onPause();  super.onPause(); }
    @Override protected void onResume() { super.onResume(); web.onResume(); }

    @Override
    protected void onSaveInstanceState(Bundle b) {
        super.onSaveInstanceState(b);
        web.saveState(b);
    }
}
