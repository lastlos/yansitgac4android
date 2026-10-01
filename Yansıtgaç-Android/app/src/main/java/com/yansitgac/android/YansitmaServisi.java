package com.yansitgac.android;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.annotation.TargetApi;
import android.app.Service;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ServiceInfo;
import android.hardware.display.DisplayManager;
import android.hardware.display.VirtualDisplay;
import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioPlaybackCaptureConfiguration;
import android.media.AudioRecord;
import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaFormat;
import android.media.projection.MediaProjection;
import android.media.projection.MediaProjectionManager;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Display;
import android.view.Surface;

import java.io.BufferedOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.ByteBuffer;

/**
 * Ekranı H.264 olarak kodlayıp 127.0.0.1:27183 üzerinden tek bir istemciye (PC) gönderir.
 * Protokol: proje kökünde docs/protokol.md (PC tarafı src/protokol.hpp ile aynı olmalı).
 *
 * Ekran dönünce bağlantı kopmaz: sanal ekran yeniden boyutlanır, kodlayıcı yeni boyutla
 * yeniden başlatılır; yeni SPS/PPS ile gelen anahtar kare PC'deki çözücüyü yeni boyuta geçirir.
 *
 * Ses (Android 10+, RECORD_AUDIO izni gerekir): uygulamaların çaldığı ses AudioPlaybackCapture ile
 * yakalanır ve ham PCM (48 kHz, 16 bit, stereo) olarak aynı bağlantıdan gönderilir.
 */
public class YansitmaServisi extends Service {

    static final String EK_SONUC_KODU = "sonucKodu";
    static final String EK_VERI = "veri";
    static final String EYLEM_DURDUR = "com.yansitgac.android.DURDUR";

    static volatile boolean calisiyor;

    private static final String ETIKET = "Yansitgac";
    private static final int PORT = 27183;
    private static final int AZAMI_KENAR = 1600;    // uzun kenar sınırı (kodlayıcı limitleri için)
    private static final int BIT_HIZI = 6_000_000;
    private static final int BILDIRIM_NO = 1;
    private static final String KANAL = "yansitma";

    private static final byte TUR_KARE = 0, TUR_AYAR = 1, TUR_ANAHTAR = 2, TUR_SES = 3;
    private static final int PROTOKOL_SURUMU = 2;
    private static final int BAYRAK_SES = 1;  // başlık bayt 5: bu oturumda ses gönderilecek

    private static final int SES_HIZI = 48000;
    private static final int SES_PARCA = SES_HIZI / 50 * 4;  // 20 ms, 16 bit stereo

    private MediaProjection projeksiyon;
    private VirtualDisplay sanalEkran;
    private ServerSocket sunucu;
    private volatile Socket etkinIstemci;
    private final Object oturumKilidi = new Object();
    private DisplayManager ekranYoneticisi;
    private DisplayManager.DisplayListener ekranDinleyici;
    private int dpi;
    // Kodlayıcının şu an kullandığı boyut (oturum iş parçacığı yazar).
    private volatile int genislik, yukseklik;
    // Ana ekran boyutu değişti (döndürme, katlanabilir ekran açılması vb.).
    private volatile boolean boyutDegisti;

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public int onStartCommand(Intent intent, int bayraklar, int baslangicNo) {
        if (intent == null || EYLEM_DURDUR.equals(intent.getAction())) {
            stopSelf();
            return START_NOT_STICKY;
        }
        if (projeksiyon != null) return START_NOT_STICKY;  // zaten çalışıyor

        // Android 14+: getMediaProjection'dan ÖNCE ön plan servisi olunmalı.
        onPlanaGec();

        MediaProjectionManager mpm = getSystemService(MediaProjectionManager.class);
        Intent veri = intent.getParcelableExtra(EK_VERI);
        projeksiyon = mpm.getMediaProjection(intent.getIntExtra(EK_SONUC_KODU, 0), veri);
        if (projeksiyon == null) {
            stopSelf();
            return START_NOT_STICKY;
        }
        Handler anaIs = new Handler(Looper.getMainLooper());
        projeksiyon.registerCallback(new MediaProjection.Callback() {
            @Override
            public void onStop() {
                stopSelf();  // kullanıcı paylaşımı sistemden durdurdu
            }
        }, anaIs);

        ekranYoneticisi = getSystemService(DisplayManager.class);
        dpi = getResources().getDisplayMetrics().densityDpi;
        int[] boyut = hedefBoyut();
        genislik = boyut[0];
        yukseklik = boyut[1];
        // Android 14+ aynı projeksiyonla yalnızca bir kez sanal ekran oluşturulabilir;
        // her yeni istemcide yüzey (setSurface), dönüşte boyut (resize) değiştirilir.
        sanalEkran = projeksiyon.createVirtualDisplay("Yansitgac", genislik, yukseklik, dpi,
                DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR, null, null, null);

        ekranDinleyici = new DisplayManager.DisplayListener() {
            @Override
            public void onDisplayChanged(int ekranNo) {
                if (ekranNo != Display.DEFAULT_DISPLAY) return;
                // Parlaklık/yenileme hızı gibi değişikliklerde de çağrılır; yalnızca boyut önemli.
                int[] yeni = hedefBoyut();
                if (yeni[0] != genislik || yeni[1] != yukseklik) boyutDegisti = true;
            }

            @Override
            public void onDisplayAdded(int ekranNo) {
            }

            @Override
            public void onDisplayRemoved(int ekranNo) {
            }
        };
        ekranYoneticisi.registerDisplayListener(ekranDinleyici, anaIs);

        calisiyor = true;
        Thread dinleyici = new Thread(this::dinle, "yansitgac-dinleyici");
        dinleyici.setDaemon(true);
        dinleyici.start();
        return START_NOT_STICKY;
    }

    @Override
    public void onDestroy() {
        calisiyor = false;
        if (ekranDinleyici != null) ekranYoneticisi.unregisterDisplayListener(ekranDinleyici);
        kapat(sunucu);
        kapat(etkinIstemci);
        if (sanalEkran != null) sanalEkran.release();
        if (projeksiyon != null) projeksiyon.stop();
        sanalEkran = null;
        projeksiyon = null;
        super.onDestroy();
    }

    private void onPlanaGec() {
        NotificationManager nm = getSystemService(NotificationManager.class);
        Notification.Builder b;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            nm.createNotificationChannel(new NotificationChannel(
                    KANAL, "Ekran yansıtma", NotificationManager.IMPORTANCE_LOW));
            b = new Notification.Builder(this, KANAL);
        } else {
            b = new Notification.Builder(this);
        }
        PendingIntent durdur = PendingIntent.getService(this, 0,
                new Intent(this, YansitmaServisi.class).setAction(EYLEM_DURDUR),
                PendingIntent.FLAG_IMMUTABLE);
        PendingIntent ac = PendingIntent.getActivity(this, 0,
                new Intent(this, AnaEtkinlik.class), PendingIntent.FLAG_IMMUTABLE);
        Notification bildirim = b.setSmallIcon(R.drawable.simge)
                .setContentTitle("Yansıtgaç")
                .setContentText("Ekran bilgisayara yansıtılıyor")
                .setContentIntent(ac)
                .setOngoing(true)
                .addAction(new Notification.Action.Builder(null, "Durdur", durdur).build())
                .build();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(BILDIRIM_NO, bildirim,
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION);
        } else {
            startForeground(BILDIRIM_NO, bildirim);
        }
    }

    /** Ana ekranın şu anki (döndürülmüş) boyutundan kodlayıcı boyutunu hesaplar. */
    @SuppressWarnings("deprecation")
    private int[] hedefBoyut() {
        DisplayMetrics m = new DisplayMetrics();
        ekranYoneticisi.getDisplay(Display.DEFAULT_DISPLAY).getRealMetrics(m);
        int g = m.widthPixels, y = m.heightPixels;
        double olcek = Math.min(1.0, (double) AZAMI_KENAR / Math.max(g, y));
        // Kodlayıcılar 16'nın katlarını sever.
        return new int[]{Math.max(16, ((int) (g * olcek)) & ~15),
                Math.max(16, ((int) (y * olcek)) & ~15)};
    }

    /** Tek seferde tek istemci: yeni bağlanan eskisini düşürür. */
    private void dinle() {
        try {
            sunucu = new ServerSocket();
            sunucu.setReuseAddress(true);
            sunucu.bind(new InetSocketAddress(InetAddress.getByName("127.0.0.1"), PORT));
            while (calisiyor) {
                Socket istemci = sunucu.accept();
                kapat(etkinIstemci);
                etkinIstemci = istemci;
                Thread t = new Thread(() -> oturum(istemci), "yansitgac-oturum");
                t.setDaemon(true);
                t.start();
            }
        } catch (IOException e) {
            if (calisiyor) Log.e(ETIKET, "Sunucu hatası", e);
        }
    }

    private void oturum(Socket istemci) {
        synchronized (oturumKilidi) {  // önceki oturumun kodlayıcıyı bırakmasını bekle
            if (istemci.isClosed() || !calisiyor) return;
            try {
                istemci.setTcpNoDelay(true);
                PaketYazici cikis = new PaketYazici(
                        new BufferedOutputStream(istemci.getOutputStream(), 1 << 16));
                boolean ses = sesMumkun();
                cikis.baslik(genislik, yukseklik, ses ? BAYRAK_SES : 0);
                if (ses) {
                    Thread t = new Thread(() -> sesiAktar(istemci, cikis), "yansitgac-ses");
                    t.setDaemon(true);
                    t.start();
                }

                // Her tur bir kodlayıcı ömrü; ekran dönünce yeni boyutla yeni tur.
                while (calisiyor && !istemci.isClosed()) {
                    boyutDegisti = false;
                    int[] boyut = hedefBoyut();
                    if (boyut[0] != genislik || boyut[1] != yukseklik) {
                        genislik = boyut[0];
                        yukseklik = boyut[1];
                        sanalEkran.resize(genislik, yukseklik, dpi);
                    }
                    kodla(istemci, cikis);
                }
            } catch (IOException | IllegalStateException e) {
                Log.i(ETIKET, "Oturum bitti: " + e);
            } finally {
                kapat(istemci);
            }
        }
    }

    /** Boyut değişene ya da bağlantı kopana kadar ekranı kodlayıp gönderir. */
    private void kodla(Socket istemci, PaketYazici cikis) throws IOException {
        MediaCodec kodlayici = null;
        Surface yuzey = null;
        try {
            MediaFormat bicim = MediaFormat.createVideoFormat(
                    MediaFormat.MIMETYPE_VIDEO_AVC, genislik, yukseklik);
            bicim.setInteger(MediaFormat.KEY_COLOR_FORMAT,
                    MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface);
            bicim.setInteger(MediaFormat.KEY_BIT_RATE, BIT_HIZI);
            bicim.setInteger(MediaFormat.KEY_FRAME_RATE, 60);
            bicim.setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 2);
            // Ekran değişmese de kare üret (PC tarafı donmuş görünmesin).
            bicim.setLong(MediaFormat.KEY_REPEAT_PREVIOUS_FRAME_AFTER, 100_000);

            Log.i(ETIKET, "Kodlayıcı başlıyor: " + genislik + "x" + yukseklik);
            kodlayici = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_VIDEO_AVC);
            kodlayici.configure(bicim, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
            yuzey = kodlayici.createInputSurface();
            kodlayici.start();
            sanalEkran.setSurface(yuzey);

            MediaCodec.BufferInfo bilgi = new MediaCodec.BufferInfo();
            byte[] tampon = new byte[1 << 20];
            long sonKontrol = 0;
            while (calisiyor && !istemci.isClosed() && !boyutDegisti) {
                // DisplayListener olayı döndürme uygulanmadan önce gelebiliyor (Android 14'te
                // görüldü) ve ardından ikinci olay gelmiyor; bu yüzden boyutu ayrıca yokla.
                long simdi = System.nanoTime();
                if (simdi - sonKontrol > 250_000_000L) {
                    sonKontrol = simdi;
                    int[] b = hedefBoyut();
                    if (b[0] != genislik || b[1] != yukseklik) {
                        Log.i(ETIKET, "Boyut değişti: " + b[0] + "x" + b[1]);
                        break;
                    }
                }
                int i = kodlayici.dequeueOutputBuffer(bilgi, 100_000);
                if (i < 0) continue;
                ByteBuffer veri = kodlayici.getOutputBuffer(i);
                if (bilgi.size > 0 && veri != null) {
                    if (tampon.length < bilgi.size) tampon = new byte[bilgi.size];
                    veri.position(bilgi.offset);
                    veri.get(tampon, 0, bilgi.size);

                    byte tur = (bilgi.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0 ? TUR_AYAR
                            : (bilgi.flags & MediaCodec.BUFFER_FLAG_KEY_FRAME) != 0 ? TUR_ANAHTAR
                            : TUR_KARE;
                    cikis.paket(tur, tampon, bilgi.size, bilgi.presentationTimeUs);
                }
                kodlayici.releaseOutputBuffer(i, false);
                if ((bilgi.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) {
                    kapat(istemci);
                    break;
                }
            }
        } finally {
            if (sanalEkran != null) sanalEkran.setSurface(null);
            if (kodlayici != null) {
                try {
                    kodlayici.stop();
                } catch (IllegalStateException ignored) {
                }
                kodlayici.release();
            }
            if (yuzey != null) yuzey.release();
        }
    }

    private boolean sesMumkun() {
        return Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q
                && checkSelfPermission(android.Manifest.permission.RECORD_AUDIO)
                == PackageManager.PERMISSION_GRANTED;
    }

    /** Bağlantı açık kaldıkça uygulamaların sesini yakalayıp gönderir. */
    @TargetApi(Build.VERSION_CODES.Q)
    private void sesiAktar(Socket istemci, PaketYazici cikis) {
        AudioRecord kayit = null;
        try {
            AudioPlaybackCaptureConfiguration ayar =
                    new AudioPlaybackCaptureConfiguration.Builder(projeksiyon)
                            .addMatchingUsage(AudioAttributes.USAGE_MEDIA)
                            .addMatchingUsage(AudioAttributes.USAGE_GAME)
                            .addMatchingUsage(AudioAttributes.USAGE_UNKNOWN)
                            .build();
            AudioFormat bicim = new AudioFormat.Builder()
                    .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                    .setSampleRate(SES_HIZI)
                    .setChannelMask(AudioFormat.CHANNEL_IN_STEREO)
                    .build();
            int enAz = AudioRecord.getMinBufferSize(SES_HIZI, AudioFormat.CHANNEL_IN_STEREO,
                    AudioFormat.ENCODING_PCM_16BIT);
            kayit = new AudioRecord.Builder()
                    .setAudioFormat(bicim)
                    .setBufferSizeInBytes(Math.max(enAz, SES_PARCA * 8))
                    .setAudioPlaybackCaptureConfig(ayar)
                    .build();
            kayit.startRecording();
            Log.i(ETIKET, "Ses yakalama başladı");
            telefonuSustur(true);

            byte[] tampon = new byte[SES_PARCA];
            long baslangic = System.nanoTime();
            while (calisiyor && !istemci.isClosed()) {
                int n = kayit.read(tampon, 0, tampon.length);
                if (n < 0) break;
                if (n > 0) cikis.paket(TUR_SES, tampon, n, (System.nanoTime() - baslangic) / 1000);
            }
        } catch (IOException | RuntimeException e) {
            Log.i(ETIKET, "Ses aktarımı bitti: " + e);
        } finally {
            telefonuSustur(false);
            if (kayit != null) {
                try {
                    kayit.stop();
                } catch (IllegalStateException ignored) {
                }
                kayit.release();
            }
        }
    }

    /**
     * Ses bilgisayara aktarılırken telefonun hoparlöründen de çıkmasın diye medya sesi sessize
     * alınır (seviye korunur), aktarım bitince geri açılır. Yakalama ses seviyesinden önce
     * yapıldığı için bilgisayara giden ses etkilenmez. Telefonda ses tuşuna basılırsa sessiz
     * kipten çıkar; bu, öğretmenin isterse telefondan da dinleyebilmesini sağlar.
     */
    private void telefonuSustur(boolean sustur) {
        try {
            AudioManager am = getSystemService(AudioManager.class);
            am.adjustStreamVolume(AudioManager.STREAM_MUSIC,
                    sustur ? AudioManager.ADJUST_MUTE : AudioManager.ADJUST_UNMUTE, 0);
        } catch (RuntimeException e) {
            Log.i(ETIKET, "Telefon sesi değiştirilemedi: " + e);
        }
    }

    /** Görüntü ve ses iş parçacıkları aynı bağlantıya yazar; paketler karışmasın diye kilitli. */
    private static final class PaketYazici {
        private final OutputStream cikis;
        private final byte[] pb = new byte[16];

        PaketYazici(OutputStream cikis) {
            this.cikis = cikis;
        }

        synchronized void baslik(int gen, int yuk, int bayraklar) throws IOException {
            byte[] b = new byte[12];
            b[0] = 'Y'; b[1] = 'N'; b[2] = 'S'; b[3] = 'G';
            b[4] = PROTOKOL_SURUMU;
            b[5] = (byte) bayraklar;
            b[6] = (byte) (gen >> 8); b[7] = (byte) gen;
            b[8] = (byte) (yuk >> 8); b[9] = (byte) yuk;
            cikis.write(b);
            cikis.flush();
        }

        synchronized void paket(byte tur, byte[] veri, int boyut, long pts) throws IOException {
            pb[0] = tur; pb[1] = 0; pb[2] = 0; pb[3] = 0;
            for (int k = 0; k < 4; k++) pb[4 + k] = (byte) (boyut >> (24 - 8 * k));
            for (int k = 0; k < 8; k++) pb[8 + k] = (byte) (pts >> (56 - 8 * k));
            cikis.write(pb);
            cikis.write(veri, 0, boyut);
            cikis.flush();
        }
    }

    private static void kapat(java.io.Closeable c) {
        if (c == null) return;
        try {
            c.close();
        } catch (IOException ignored) {
        }
    }
}
