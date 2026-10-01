#include "adb.hpp"

#include <glib.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <sstream>

namespace {

bool calistirilabilir(const std::string& yol) {
    return !yol.empty() && access(yol.c_str(), X_OK) == 0;
}
bool okunabilir(const std::string& yol) {
    return !yol.empty() && access(yol.c_str(), R_OK) == 0;
}

std::string exe_dizini() {
    char tampon[4096];
    ssize_t n = readlink("/proc/self/exe", tampon, sizeof(tampon) - 1);
    if (n <= 0) return {};
    tampon[n] = '\0';
    gchar* dizin = g_path_get_dirname(tampon);
    std::string s = dizin;
    g_free(dizin);
    return s;
}

std::string ortam(const char* ad) {
    const char* d = std::getenv(ad);
    return d ? d : "";
}

}  // namespace

namespace adb {

std::string adb_yolu() {
    static const std::string yol = [] {
        std::vector<std::string> adaylar = {ortam("YANSITGAC_ADB")};
        if (!ortam("APPDIR").empty()) adaylar.push_back(ortam("APPDIR") + "/usr/bin/adb");
        adaylar.push_back(exe_dizini() + "/adb");
        for (auto& a : adaylar)
            if (calistirilabilir(a)) return a;
        if (gchar* p = g_find_program_in_path("adb")) {
            std::string s = p;
            g_free(p);
            return s;
        }
        std::string sdk = ortam("HOME") + "/Android/Sdk/platform-tools/adb";
        return calistirilabilir(sdk) ? sdk : std::string();
    }();
    return yol;
}

bool gomulu_mu() {
    const std::string appdir = ortam("APPDIR");
    return !appdir.empty() && adb_yolu().rfind(appdir, 0) == 0;
}

std::string apk_yolu() {
    static const std::string yol = [] {
        std::vector<std::string> adaylar = {ortam("YANSITGAC_APK")};
        if (!ortam("APPDIR").empty())
            adaylar.push_back(ortam("APPDIR") + "/usr/share/yansitgac/yansitgac.apk");
        adaylar.push_back(exe_dizini() + "/yansitgac.apk");
#ifdef YANSITGAC_GELISTIRME_APK
        adaylar.push_back(YANSITGAC_GELISTIRME_APK);
#endif
        for (auto& a : adaylar)
            if (okunabilir(a)) return a;
        return std::string();
    }();
    return yol;
}

AdbSonuc calistir(const std::vector<std::string>& argumanlar) {
    AdbSonuc sonuc;
    const std::string yol = adb_yolu();
    if (yol.empty()) {
        sonuc.cikti = "adb bulunamadı";
        return sonuc;
    }
    std::vector<gchar*> argv;
    argv.push_back(const_cast<gchar*>(yol.c_str()));
    for (auto& a : argumanlar) argv.push_back(const_cast<gchar*>(a.c_str()));
    argv.push_back(nullptr);

    gchar* cikti = nullptr;
    gchar* hata = nullptr;
    gint durum = 0;
    GError* gerr = nullptr;
    if (!g_spawn_sync(nullptr, argv.data(), nullptr, G_SPAWN_DEFAULT, nullptr, nullptr, &cikti,
                      &hata, &durum, &gerr)) {
        sonuc.cikti = gerr ? gerr->message : "adb çalıştırılamadı";
        g_clear_error(&gerr);
        return sonuc;
    }
    sonuc.cikti = std::string(cikti ? cikti : "") + (hata ? hata : "");
    g_free(cikti);
    g_free(hata);
    sonuc.kod = WIFEXITED(durum) ? WEXITSTATUS(durum) : 1;
    return sonuc;
}

AdbSonuc calistir(const std::string& seri, const std::vector<std::string>& argumanlar) {
    std::vector<std::string> tam = {"-s", seri};
    tam.insert(tam.end(), argumanlar.begin(), argumanlar.end());
    return calistir(tam);
}

Cihaz cihaz_bul() {
    Cihaz secilen;
    // ANDROID_SERIAL verilmişse (adb'nin kendi kuralı) yalnızca o cihaz kullanılır.
    const std::string istenen = ortam("ANDROID_SERIAL");
    AdbSonuc s = calistir({"devices"});
    if (!s.basarili()) return secilen;

    std::istringstream akis(s.cikti);
    std::string satir;
    while (std::getline(akis, satir)) {
        if (satir.empty() || satir[0] == '*' || satir.rfind("List of devices", 0) == 0) continue;
        auto sekme = satir.find('\t');
        if (sekme == std::string::npos) continue;
        Cihaz c;
        c.seri = satir.substr(0, sekme);
        if (!istenen.empty() && c.seri != istenen) continue;
        std::string d = satir.substr(sekme + 1);
        if (d == "device") c.durum = CihazDurumu::HAZIR;
        else if (d == "unauthorized") c.durum = CihazDurumu::YETKISIZ;
        else if (d.rfind("no permissions", 0) == 0) c.durum = CihazDurumu::IZIN_YOK;
        else c.durum = CihazDurumu::CEVRIMDISI;

        if (c.durum == CihazDurumu::HAZIR) return c;
        if (secilen.durum == CihazDurumu::YOK) secilen = c;
    }
    return secilen;
}

long kurulu_surum(const std::string& seri, const std::string& paket) {
    AdbSonuc s = calistir(seri, {"shell", "dumpsys", "package", paket});
    if (!s.basarili()) return -1;
    auto konum = s.cikti.find("versionCode=");
    if (konum == std::string::npos) return -1;
    return std::strtol(s.cikti.c_str() + konum + 12, nullptr, 10);
}

}  // namespace adb
