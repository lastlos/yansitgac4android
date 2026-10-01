// Yansıtgaç — USB ile bağlı Android telefonun ekranını gösteren GTK2 penceresi.
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <unistd.h>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "adb.hpp"
#include "oturum.hpp"
#include "ses.hpp"

namespace {

// İş parçacığından gelen son kare; GTK ana döngüsü buradan çizer.
struct KareKutusu {
    std::mutex kilit;
    std::vector<uint8_t> bgra;
    int gen = 0, yuk = 0;
    bool akis_var = false;
    std::string durum = "Başlatılıyor…";
};

struct Uygulama {
    GtkWidget* pencere = nullptr;
    GtkWidget* cizim = nullptr;
    bool tam_ekran = false;
    KareKutusu kutu;
    std::atomic<bool> cizim_bekliyor{false};
    int son_gen = 0, son_yuk = 0;  // pencereyi en son uyarladığımız görüntü boyutu (iş parçacığı)
    SesCalar ses;
    std::atomic<int> ses_var{-1};  // -1 bilinmiyor (bağlı değil), 0 telefon ses göndermiyor, 1 gönderiyor
};

Uygulama* uyg = nullptr;  // tek pencere, programın ömrü boyunca yaşar

// İş parçacığından GTK'ya: yeniden çizimi ana döngüde iste (birikmesin diye bayraklı).
gboolean yeniden_ciz_bosta(gpointer) {
    uyg->cizim_bekliyor = false;
    gtk_widget_queue_draw(uyg->cizim);
    return G_SOURCE_REMOVE;
}
void yeniden_ciz_iste() {
    if (!uyg->cizim_bekliyor.exchange(true)) g_idle_add(yeniden_ciz_bosta, nullptr);
}

// Görüntünün yönü değişince (tablet döndürüldü) pencereyi yeni en-boy oranına uyarla.
// Tam ekran veya büyütülmüş pencereye dokunulmaz; orada siyah bantlarla sığdırma yeterli.
struct BoyutIstegi {
    int gen, yuk;
};
gboolean pencereyi_uyarla(gpointer veri) {
    auto* istek = static_cast<BoyutIstegi*>(veri);
    GdkWindow* gw = uyg->pencere->window;
    bool buyutulmus = gw && (gdk_window_get_state(gw) & GDK_WINDOW_STATE_MAXIMIZED);
    if (!uyg->tam_ekran && !buyutulmus) {
        GdkScreen* ekran = gtk_window_get_screen(GTK_WINDOW(uyg->pencere));
        int monitor = gw ? gdk_screen_get_monitor_at_window(ekran, gw) : 0;
        GdkRectangle alan;
        gdk_screen_get_monitor_geometry(ekran, monitor, &alan);
        // Ekranın %75'ine sığacak en büyük boyut (görüntünün kendi boyutunu aşmadan).
        double olcek = std::min({0.75 * alan.width / istek->gen, 0.75 * alan.height / istek->yuk, 1.0});
        gtk_window_resize(GTK_WINDOW(uyg->pencere), std::max(1, int(istek->gen * olcek)),
                          std::max(1, int(istek->yuk * olcek)));
    }
    delete istek;
    return G_SOURCE_REMOVE;
}

void durum_metni_ciz(cairo_t* cr, GtkWidget* w, const std::string& metin) {
    PangoLayout* duzen = pango_cairo_create_layout(cr);
    PangoFontDescription* yazi = pango_font_description_from_string("Sans 13");
    pango_layout_set_font_description(duzen, yazi);
    pango_layout_set_alignment(duzen, PANGO_ALIGN_CENTER);
    pango_layout_set_width(duzen, (w->allocation.width - 40) * PANGO_SCALE);
    pango_layout_set_wrap(duzen, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_text(duzen, metin.c_str(), -1);
    int tw, th;
    pango_layout_get_pixel_size(duzen, &tw, &th);
    cairo_set_source_rgb(cr, 0.85, 0.85, 0.85);
    cairo_move_to(cr, 20, (w->allocation.height - th) / 2.0);
    pango_cairo_show_layout(cr, duzen);
    pango_font_description_free(yazi);
    g_object_unref(duzen);
}

gboolean cizim_olayi(GtkWidget* w, GdkEventExpose*, gpointer) {
    cairo_t* cr = gdk_cairo_create(w->window);
    const double pg = w->allocation.width, py = w->allocation.height;
    cairo_set_source_rgb(cr, 0.07, 0.07, 0.08);
    cairo_paint(cr);

    {
        std::lock_guard<std::mutex> k(uyg->kutu.kilit);
        auto& kutu = uyg->kutu;
        if (kutu.akis_var && kutu.gen > 0 && kutu.yuk > 0) {
            cairo_surface_t* yuzey = cairo_image_surface_create_for_data(
                kutu.bgra.data(), CAIRO_FORMAT_RGB24, kutu.gen, kutu.yuk, kutu.gen * 4);
            // En-boy oranını koruyarak pencereye sığdır.
            double olcek = std::min(pg / kutu.gen, py / kutu.yuk);
            double ox = (pg - kutu.gen * olcek) / 2, oy = (py - kutu.yuk * olcek) / 2;
            cairo_translate(cr, ox, oy);
            cairo_scale(cr, olcek, olcek);
            cairo_set_source_surface(cr, yuzey, 0, 0);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
            cairo_paint(cr);
            cairo_surface_destroy(yuzey);
        } else {
            durum_metni_ciz(cr, w, kutu.durum);
        }
    }
    cairo_destroy(cr);
    return TRUE;
}

// Pencere simgesi kodda çizilir: simge temasından okumak gdk-pixbuf yükleyici modülleri ister,
// gömülü GTK kullanılırken sistemin yükleyicileri uyumsuz olabilir (bkz. paketleme/AppRun).
GdkPixbuf* pencere_simgesi() {
    const int b = 64;
    cairo_surface_t* y = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, b, b);
    cairo_t* cr = cairo_create(y);
    cairo_scale(cr, b / 48.0, b / 48.0);  // data/yansitgac.svg ile aynı 48'lik ızgara
    auto yuvarlak = [cr](double x, double yy, double g, double h, double r) {
        cairo_new_sub_path(cr);
        cairo_arc(cr, x + g - r, yy + r, r, -G_PI / 2, 0);
        cairo_arc(cr, x + g - r, yy + h - r, r, 0, G_PI / 2);
        cairo_arc(cr, x + r, yy + h - r, r, G_PI / 2, G_PI);
        cairo_arc(cr, x + r, yy + r, r, G_PI, 3 * G_PI / 2);
        cairo_close_path(cr);
    };
    cairo_set_source_rgb(cr, 0x2B / 255.0, 0x6C / 255.0, 0xB0 / 255.0);
    yuvarlak(2, 2, 44, 44, 6);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 1, 1, 1);
    yuvarlak(10, 9, 16, 30, 2);  // telefon çerçevesi
    cairo_rectangle(cr, 13, 12, 10, 22);
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
    cairo_fill(cr);
    cairo_move_to(cr, 29, 21);  // yansıma oku
    cairo_line_to(cr, 37, 21);
    cairo_line_to(cr, 37, 17);
    cairo_line_to(cr, 44, 24);
    cairo_line_to(cr, 37, 31);
    cairo_line_to(cr, 37, 27);
    cairo_line_to(cr, 29, 27);
    cairo_close_path(cr);
    cairo_fill(cr);
    cairo_destroy(cr);
    cairo_surface_flush(y);

    // cairo ARGB32 (önçarpımlı, yerel endian) → GdkPixbuf RGBA
    GdkPixbuf* pb = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, b, b);
    const uint8_t* kaynak = cairo_image_surface_get_data(y);
    const int kadim = cairo_image_surface_get_stride(y);
    uint8_t* hedef = gdk_pixbuf_get_pixels(pb);
    const int hadim = gdk_pixbuf_get_rowstride(pb);
    for (int sat = 0; sat < b; ++sat) {
        for (int s = 0; s < b; ++s) {
            uint32_t p = *reinterpret_cast<const uint32_t*>(kaynak + sat * kadim + s * 4);
            uint8_t a = p >> 24;
            uint8_t* h = hedef + sat * hadim + s * 4;
            auto ac = [a](uint32_t k) { return uint8_t(a ? std::min(255u, k * 255 / a) : 0); };
            h[0] = ac((p >> 16) & 0xFF);
            h[1] = ac((p >> 8) & 0xFF);
            h[2] = ac(p & 0xFF);
            h[3] = a;
        }
    }
    cairo_surface_destroy(y);
    return pb;
}

void tam_ekran_degistir() {
    if (uyg->tam_ekran) gtk_window_unfullscreen(GTK_WINDOW(uyg->pencere));
    else gtk_window_fullscreen(GTK_WINDOW(uyg->pencere));
    uyg->tam_ekran = !uyg->tam_ekran;
}

// Pencere başlığı sesin durumunu gösterir (ana iş parçacığında çağrılır).
gboolean basligi_guncelle(gpointer = nullptr) {
    std::string baslik = "Yansıtgaç";
    const int var = uyg->ses_var;
    if (var == 0) baslik += " — ses yok (telefonda Android 10 ve üstü gerekir)";
    else if (var == 1 && uyg->ses.sessiz()) baslik += " — ses kapalı (açmak için M)";
    gtk_window_set_title(GTK_WINDOW(uyg->pencere), baslik.c_str());
    return G_SOURCE_REMOVE;
}

gboolean tus_olayi(GtkWidget*, GdkEventKey* e, gpointer) {
    if (e->keyval == GDK_m || e->keyval == GDK_M) {
        uyg->ses.sessiz(!uyg->ses.sessiz());
        basligi_guncelle();
        return TRUE;
    }
    if (e->keyval == GDK_F11) {
        tam_ekran_degistir();
        return TRUE;
    }
    if (e->keyval == GDK_Escape && uyg->tam_ekran) {
        tam_ekran_degistir();
        return TRUE;
    }
    return FALSE;
}

gboolean tiklama_olayi(GtkWidget*, GdkEventButton* e, gpointer) {
    if (e->type == GDK_2BUTTON_PRESS && e->button == 1) tam_ekran_degistir();
    return FALSE;
}

// adb anahtarını ~/.android altında tutar; ev dizini yoksa veya yazılamıyorsa (kiosk/misafir
// hesapları, konteyner) adb çöker. O durumda adb'ye geçici bir ev dizini ver.
void ev_dizinini_hazirla() {
    const char* ev = std::getenv("HOME");
    if (ev && *ev && access(ev, W_OK) == 0) return;
    std::string gecici = std::string(g_get_tmp_dir()) + "/yansitgac-" + std::to_string(getuid());
    g_mkdir_with_parents(gecici.c_str(), 0700);
    setenv("HOME", gecici.c_str(), 1);
}

}  // namespace

int main(int argc, char** argv) {
    g_set_prgname("yansitgac");  // WM_CLASS: görev çubuğunda "AppRun" yerine
    gtk_init(&argc, &argv);
    ev_dizinini_hazirla();

    // --dogrudan PORT: adb olmadan 127.0.0.1:PORT'a bağlan (geliştirme/test).
    int dogrudan_port = 0;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--dogrudan") == 0 && i + 1 < argc)
            dogrudan_port = std::atoi(argv[++i]);

    uyg = new Uygulama;

    uyg->pencere = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(uyg->pencere), "Yansıtgaç");
    gtk_window_set_default_size(GTK_WINDOW(uyg->pencere), 420, 800);
    GdkPixbuf* simge = pencere_simgesi();
    gtk_window_set_icon(GTK_WINDOW(uyg->pencere), simge);
    g_object_unref(simge);
    g_signal_connect(uyg->pencere, "destroy", G_CALLBACK(gtk_main_quit), nullptr);
    g_signal_connect(uyg->pencere, "key-press-event", G_CALLBACK(tus_olayi), nullptr);

    uyg->cizim = gtk_drawing_area_new();
    gtk_widget_set_double_buffered(uyg->cizim, TRUE);
    gtk_widget_add_events(uyg->cizim, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(uyg->cizim, "expose-event", G_CALLBACK(cizim_olayi), nullptr);
    g_signal_connect(uyg->cizim, "button-press-event", G_CALLBACK(tiklama_olayi), nullptr);
    gtk_container_add(GTK_CONTAINER(uyg->pencere), uyg->cizim);
    gtk_widget_show_all(uyg->pencere);

    Oturum::Olaylar olaylar;
    olaylar.durum = [](const std::string& m) {
        {
            std::lock_guard<std::mutex> k(uyg->kutu.kilit);
            uyg->kutu.durum = m;
        }
        yeniden_ciz_iste();
    };
    olaylar.kare = [](const uint8_t* bgra, int gen, int yuk) {
        {
            std::lock_guard<std::mutex> k(uyg->kutu.kilit);
            uyg->kutu.bgra.assign(bgra, bgra + size_t(gen) * yuk * 4);
            uyg->kutu.gen = gen;
            uyg->kutu.yuk = yuk;
        }
        // İlk karede ve yön değişince (dikey ↔ yatay) pencereyi uyarla.
        if (uyg->son_gen == 0 || (gen > yuk) != (uyg->son_gen > uyg->son_yuk))
            g_idle_add(pencereyi_uyarla, new BoyutIstegi{gen, yuk});
        uyg->son_gen = gen;
        uyg->son_yuk = yuk;
        yeniden_ciz_iste();
    };
    olaylar.akis = [](bool var) {
        {
            std::lock_guard<std::mutex> k(uyg->kutu.kilit);
            uyg->kutu.akis_var = var;
            if (!var) uyg->kutu.gen = uyg->kutu.yuk = 0;
        }
        if (!var) {
            uyg->son_gen = uyg->son_yuk = 0;  // yeni cihaz/bağlantıda yeniden uyarla
            uyg->ses.temizle();
            uyg->ses_var = -1;
            g_idle_add(basligi_guncelle, nullptr);
        }
        yeniden_ciz_iste();
    };
    olaylar.ses = [](const uint8_t* pcm, size_t boyut) { uyg->ses.yaz(pcm, boyut); };
    olaylar.ses_durumu = [](bool var) {
        uyg->ses_var = var ? 1 : 0;
        g_idle_add(basligi_guncelle, nullptr);
    };

    auto* oturum = new Oturum(olaylar, dogrudan_port);
    oturum->baslat();

    gtk_main();

    // İş parçacığı bir adb komutunda bekliyor olabilir; beklemeden çık.
    oturum->durdur();
    if (adb::gomulu_mu()) adb::calistir({"kill-server"});
    std::_Exit(0);
}
