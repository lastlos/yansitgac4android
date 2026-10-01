package com.yansitgac.android;

import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioTrack;

/** 3 saniyelik 440 Hz ton çalar; ses aktarımını telefonda başka uygulama açmadan sınamak için. */
final class SesDenemesi {
    private SesDenemesi() {
    }

    static void cal() {
        new Thread(() -> {
            final int hiz = 48000, sure = 3;
            short[] ornek = new short[hiz * sure * 2];
            for (int i = 0; i < hiz * sure; i++) {
                short d = (short) (Math.sin(2 * Math.PI * 440 * i / hiz) * 8000);
                ornek[2 * i] = d;
                ornek[2 * i + 1] = d;
            }
            AudioTrack iz = new AudioTrack.Builder()
                    .setAudioAttributes(new AudioAttributes.Builder()
                            .setUsage(AudioAttributes.USAGE_MEDIA)
                            .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
                    .setAudioFormat(new AudioFormat.Builder()
                            .setEncoding(AudioFormat.ENCODING_PCM_16BIT).setSampleRate(hiz)
                            .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO).build())
                    .setTransferMode(AudioTrack.MODE_STATIC)
                    .setBufferSizeInBytes(ornek.length * 2)
                    .build();
            iz.write(ornek, 0, ornek.length);
            iz.play();
            try {
                Thread.sleep(sure * 1000L + 200);
            } catch (InterruptedException ignored) {
            }
            iz.release();
        }, "ses-denemesi").start();
    }
}
