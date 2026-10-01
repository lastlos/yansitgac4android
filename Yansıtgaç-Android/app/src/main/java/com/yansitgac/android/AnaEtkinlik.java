package com.yansitgac.android;

import android.app.Activity;
import android.content.Intent;
import android.media.projection.MediaProjectionManager;
import android.os.Build;
import android.os.Bundle;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

/**
 * Ekran paylaşım iznini ister ve YansitmaServisi'ni başlatır.
 * PC programı bu etkinliği "adb shell am start" ile açar; izin otomatik istenir.
 */
public class AnaEtkinlik extends Activity {

    private static final int IZIN_ISTEGI = 1;
    private static final int SES_IZNI_ISTEGI = 2;
    /** Test için: "am start ... --ez sesDenemesi true" 3 sn'lik bir ton çalar (ses aktarımını sınamak). */
    static final String EK_SES_DENEMESI = "sesDenemesi";

    private TextView durumYazisi;
    private Button dugme;
    private boolean izinBekleniyor;
    private boolean sesIzniSoruldu;

    @Override
    protected void onCreate(Bundle kayitli) {
        super.onCreate(kayitli);

        LinearLayout duzen = new LinearLayout(this);
        duzen.setOrientation(LinearLayout.VERTICAL);
        duzen.setGravity(Gravity.CENTER);
        int bosluk = (int) (24 * getResources().getDisplayMetrics().density);
        duzen.setPadding(bosluk, bosluk, bosluk, bosluk);

        durumYazisi = new TextView(this);
        durumYazisi.setTextSize(18);
        durumYazisi.setGravity(Gravity.CENTER);
        duzen.addView(durumYazisi);

        dugme = new Button(this);
        LinearLayout.LayoutParams dp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        dp.topMargin = bosluk;
        duzen.addView(dugme, dp);
        dugme.setOnClickListener(v -> {
            if (YansitmaServisi.calisiyor) {
                startService(new Intent(this, YansitmaServisi.class)
                        .setAction(YansitmaServisi.EYLEM_DURDUR));
                guncelle(false);
            } else {
                izinIste();
            }
        });

        setContentView(duzen);

        izinBekleniyor = kayitli != null && kayitli.getBoolean("izinBekleniyor");
        if (getIntent().getBooleanExtra(EK_SES_DENEMESI, false)) SesDenemesi.cal();
        if (!YansitmaServisi.calisiyor && !izinBekleniyor) izinIste();
    }

    @Override
    protected void onSaveInstanceState(Bundle cikis) {
        super.onSaveInstanceState(cikis);
        cikis.putBoolean("izinBekleniyor", izinBekleniyor);
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        if (intent.getBooleanExtra(EK_SES_DENEMESI, false)) {
            SesDenemesi.cal();
            return;
        }
        // PC yeniden bağlanmak istiyor ve servis çalışmıyorsa izni tekrar iste. izinBekleniyor'a
        // bakılmaz: önceki izin penceresi sonuç dönmeden kapanmış olabilir (PC programı kapatıldı,
        // ekran kilitlendi…) ve bayrak takılı kalırsa izin bir daha hiç istenmez.
        if (!YansitmaServisi.calisiyor) izinIste();
    }

    @Override
    protected void onResume() {
        super.onResume();
        guncelle(YansitmaServisi.calisiyor);
    }

    private void izinIste() {
        // Ses izni normalde kurulumda verilir (adb install -g); verilmemişse bir kez sor.
        // Reddedilirse yansıtma sessiz devam eder.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q && !sesIzniSoruldu
                && checkSelfPermission(android.Manifest.permission.RECORD_AUDIO)
                != android.content.pm.PackageManager.PERMISSION_GRANTED) {
            sesIzniSoruldu = true;
            izinBekleniyor = true;
            requestPermissions(new String[]{android.Manifest.permission.RECORD_AUDIO},
                    SES_IZNI_ISTEGI);
            return;
        }
        MediaProjectionManager mpm = getSystemService(MediaProjectionManager.class);
        izinBekleniyor = true;
        startActivityForResult(mpm.createScreenCaptureIntent(), IZIN_ISTEGI);
    }

    @Override
    public void onRequestPermissionsResult(int istek, String[] izinler, int[] sonuclar) {
        super.onRequestPermissionsResult(istek, izinler, sonuclar);
        if (istek == SES_IZNI_ISTEGI) izinIste();
    }

    @Override
    protected void onActivityResult(int istek, int sonuc, Intent veri) {
        super.onActivityResult(istek, sonuc, veri);
        if (istek != IZIN_ISTEGI) return;
        izinBekleniyor = false;
        if (sonuc != RESULT_OK || veri == null) {
            guncelle(false);
            return;
        }
        Intent servis = new Intent(this, YansitmaServisi.class)
                .putExtra(YansitmaServisi.EK_SONUC_KODU, sonuc)
                .putExtra(YansitmaServisi.EK_VERI, veri);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) startForegroundService(servis);
        else startService(servis);
        guncelle(true);
        // Kullanıcı telefonu kullanmaya devam edebilsin.
        moveTaskToBack(true);
    }

    private void guncelle(boolean calisiyor) {
        if (calisiyor) {
            durumYazisi.setText("Yansıtma etkin.\nEkran USB üzerinden bilgisayara aktarılıyor.");
            dugme.setText("Yansıtmayı durdur");
        } else {
            durumYazisi.setText("Yansıtma kapalı.\n\nBilgisayarda Yansıtgaç'ı açıp telefonu "
                    + "USB ile bağladığınızda burada ekran paylaşım izni istenir.");
            dugme.setText("Yansıtmayı başlat");
        }
    }
}
