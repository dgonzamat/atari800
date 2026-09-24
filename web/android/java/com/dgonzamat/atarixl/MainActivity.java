package com.dgonzamat.atarixl;

import android.app.Activity;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.webkit.JavascriptInterface;
import android.webkit.ValueCallback;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;

/* La aplicacion entera es la pagina: esta actividad solo la abre y le da lo
   que un navegador no puede, que es esconder las barras del telefono al
   jugar a pantalla completa y saber que hacer con el boton de Atras. */
public class MainActivity extends Activity {

    private WebView web;

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
    }

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
