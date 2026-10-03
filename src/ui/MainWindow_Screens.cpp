#include "ui/MainWindowInternal.h"

namespace ft {

void MainWindow::DrawComingSoon(const D2D1_RECT_F& a, const wchar_t* title,
                                const wchar_t* body, const wchar_t* milestone) {
    const float s = m_lay.scale;
    m_r.PushClip(a);
    const float x0 = a.left + std::floor(28 * s);
    const float x1 = std::min(a.right - std::floor(28 * s), x0 + std::floor(560 * s));
    float y = a.top + std::floor(28 * s);

    m_r.Text(title, D2D1::RectF(x0, y, x1, y + std::floor(34 * s)),
             theme::TextHi, 20.0f * s, Renderer::Align::Left, true);
    y += std::floor(42 * s);

    const D2D1_RECT_F pill = D2D1::RectF(x0, y, x0 + std::floor(74 * s), y + std::floor(24 * s));
    m_ui.Badge(pill, milestone, theme::Ac());
    y += std::floor(36 * s);

    m_r.Text(body, D2D1::RectF(x0, y, x1, y + std::floor(90 * s)), theme::TextMuted, 13.0f * s,
             Renderer::Align::Left, false, false, 1.0f, true);
    y += std::floor(96 * s);

    m_r.Text(L"Ister listesi docs/02-ISTER-LISTESI.md icinde.",
             D2D1::RectF(x0, y, x1, y + std::floor(22 * s)), theme::TextDim, 11.5f * s, Renderer::Align::Left, false, true);
    m_r.PopClip();
}

void MainWindow::DrawSshStageView(TerminalTab* tab, const D2D1_RECT_F& a) {
    if (!tab) return;
    const float s = m_lay.scale;
    m_r.Fill(a, theme::TermBg, m_cfg.opacity);

    const float cx = std::floor((a.left + a.right) * 0.5f);
    const float cy = std::floor((a.top + a.bottom) * 0.5f);
    const float cardW = std::min(a.right - a.left - 40 * s, std::floor(560.0f * s));
    const float x0 = cx - cardW * 0.5f;
    const float x1 = cx + cardW * 0.5f;
    const bool failed = (tab->GetSshStage() == SshStage::Failed);

    const float boxH = failed ? std::clamp(std::floor(240 * s), std::floor(180 * s), a.bottom - a.top - std::floor(180 * s))
                              : std::floor(180 * s);
    const float totalH = std::floor(130 * s) + boxH;
    float y = std::max(a.top + 20 * s, cy - totalH * 0.5f);

    // 1. Header (Sunucu ikonu, Host adi, SSH ip:port, Copy logs butonu)
    const float icSize = std::floor(40 * s);
    const D2D1_RECT_F icR = D2D1::RectF(x0, y, x0 + icSize, y + icSize);
    m_r.FillRound(icR, std::floor(10 * s), 0xE95420); // Ubuntu Turuncusu
    DrawIcon(Icon::Terminal, D2D1::RectF(icR.left + 8 * s, icR.top + 8 * s, icR.right - 8 * s, icR.bottom - 8 * s), 0xFFFFFF);

    const Host& h = tab->GetSshHost();
    const std::wstring hostDisp = h.Display().empty() ? L"SSH Sunucusu" : h.Display();
    m_r.Text(hostDisp, D2D1::RectF(icR.right + 12 * s, y, x1 - 100 * s, y + 22 * s),
             theme::TextHi, 15.0f * s, Renderer::Align::Left, true);

    const std::wstring sub = L"SSH " + (h.address.empty() ? L"127.0.0.1" : h.address) +
                             L":" + std::to_wstring(h.port ? h.port : 22);
    m_r.Text(sub, D2D1::RectF(icR.right + 12 * s, y + 22 * s, x1 - 100 * s, y + 40 * s),
             theme::TextDim, 11.5f * s);

    if (failed) {
        if (m_ui.Button(8100, D2D1::RectF(x1 - std::floor(96 * s), y + std::floor(4 * s), x1, y + std::floor(36 * s)),
                        L"Copy logs")) {
            ClipboardSetText(m_hwnd, tab->GetFormattedSshLogs());
            Toast(L"Baglanti loglari kopyalandi");
        }
    }

    y += icSize + std::floor(22 * s);

    // 2. Baglanti Gostergesi (Plug ------- Terminal)
    const float epR = std::floor(11 * s);
    const float epY = y + epR;
    const D2D1_RECT_F epLeft = D2D1::RectF(x0 + std::floor(12 * s), y, x0 + std::floor(12 * s) + epR * 2, y + epR * 2);
    const D2D1_RECT_F epRight = D2D1::RectF(x1 - std::floor(12 * s) - epR * 2, y, x1 - std::floor(12 * s), y + epR * 2);

    const uint32_t barColor = failed ? 0xF05454 : theme::Ac();
    m_r.FillRound(epLeft, epR, barColor);
    m_r.FillRound(epRight, epR, barColor);
    m_r.Line(epLeft.right, epY, epRight.left, epY, barColor, std::max(2.0f, std::floor(2.5f * s)));

    if (failed) {
        m_r.Text(L"x", epLeft, 0xFFFFFF, 11.0f * s, Renderer::Align::Center, true);
    } else {
        m_r.Text(L"~", epLeft, 0xFFFFFF, 11.0f * s, Renderer::Align::Center, true);
        const float tPulse = std::fmod(static_cast<float>(GetTickCount64()) / 1000.0f * 1.5f, 1.0f);
        const float packetX = epLeft.right + (epRight.left - epLeft.right) * tPulse;
        m_r.Disc(packetX, epY, std::floor(4.0f * s), 0xFFFFFF, 0.85f);
    }
    m_r.Text(L">_", epRight, 0xFFFFFF, 9.5f * s, Renderer::Align::Center, true);

    y += epR * 2 + std::floor(14 * s);

    if (failed) {
        m_r.Text(L"Connection failed with connection log:",
                 D2D1::RectF(x0, y, x1, y + std::floor(22 * s)),
                 0xF05454, 13.0f * s, Renderer::Align::Center, true);
    } else {
        const int dotCount = static_cast<int>((GetTickCount64() / 400) % 4);
        std::wstring dots = L"Connecting to host";
        for (int i = 0; i < dotCount; ++i) dots += L".";
        m_r.Text(dots,
                 D2D1::RectF(x0, y, x1, y + std::floor(22 * s)),
                 theme::AcHi(), 13.0f * s, Renderer::Align::Center, true);
    }

    y += std::floor(26 * s);

    // 3. Kart Kutusu (boxR)
    const D2D1_RECT_F boxR = D2D1::RectF(x0, y, x1, y + boxH);
    m_r.FillRound(boxR, std::floor(10 * s), 0x161C26);
    m_r.Stroke(boxR, failed ? 0x3D2428 : 0x2A3241, std::max(1.0f, std::floor(1 * s)));

    if (!failed) {
        // --- BAGLANIRKEN: LOGLARI GOSTERME! Donen spinner ve loading gostergesi ---
        const float spinCx = cx;
        const float spinCy = boxR.top + std::floor(54 * s);
        const float spinR = std::floor(20 * s);
        const float dotR = std::max(2.2f, std::floor(2.8f * s));
        const float baseAngle = static_cast<float>(GetTickCount64() / 1000.0 * 2.0 * 3.14159265);

        m_r.Ring(spinCx, spinCy, spinR, theme::Ac(), std::max(1.0f, std::floor(1.2f * s)), 0.15f);

        for (int i = 0; i < 8; ++i) {
            const float ang = i * (2.0f * 3.14159265f / 8.0f) + baseAngle;
            const float px = spinCx + std::cos(ang) * spinR;
            const float py = spinCy + std::sin(ang) * spinR;
            const float alpha = 0.15f + 0.85f * (float(i) / 7.0f);
            m_r.Disc(px, py, dotR, theme::AcHi(), alpha);
        }

        const float textY = spinCy + spinR + std::floor(16 * s);
        const std::wstring currentStep = tab->GetCurrentSshStep();
        m_r.Text(currentStep,
                 D2D1::RectF(boxR.left + 16 * s, textY, boxR.right - 16 * s, textY + std::floor(22 * s)),
                 theme::TextHi, 13.5f * s, Renderer::Align::Center, true);

        m_r.Text(TrText(L"SSH authentication and secure handshake in progress, please wait.",
                        L"SSH kimlik doğrulaması ve güvenli el sıkışma yapılıyor, lütfen bekleyin."),
                 D2D1::RectF(boxR.left + 16 * s, textY + std::floor(24 * s), boxR.right - 16 * s, textY + std::floor(46 * s)),
                 theme::TextDim, 11.5f * s, Renderer::Align::Center);

        m_r.Text(TrText(L"Terminal session will open automatically when connected.",
                        L"Bağlantı kurulduğunda terminal oturumu doğrudan açılacaktır."),
                 D2D1::RectF(boxR.left + 16 * s, boxR.bottom - std::floor(26 * s), boxR.right - 16 * s, boxR.bottom - std::floor(8 * s)),
                 theme::TextMuted, 10.5f * s, Renderer::Align::Center);

        // Dinamik kayan ilerleme cizgisi
        const float pulseT = std::fmod(static_cast<float>(GetTickCount64()) / 1400.0f, 1.0f);
        const float barLen = (boxR.right - boxR.left) * 0.35f;
        const float barStart = boxR.left + ((boxR.right - boxR.left) + barLen) * pulseT - barLen;
        const float barEnd = std::min(boxR.right, barStart + barLen);
        if (barEnd > boxR.left) {
            m_r.Fill(D2D1::RectF(std::max(boxR.left, barStart), boxR.bottom - std::floor(2.5f * s), barEnd, boxR.bottom), theme::Ac(), 0.85f);
        }
    } else {
        // --- BAGLANTI BASARISIZ OLDUGUNDA: AYRINTILI LOGLARI GOSTER ---
        m_r.PushClip(boxR);
        const float lpad = std::floor(14 * s);
        const float lineH = std::floor(20 * s);
        const auto& logs = tab->GetSshLogs();

        const float contentH = logs.size() * lineH;
        const float visibleH = boxH - std::floor(34 * s);
        const float maxScroll = std::max(0.0f, contentH - visibleH);

        if (m_sshLogScroll < 0.0f) {
            m_sshLogScroll = maxScroll;
        } else {
            m_sshLogScroll = std::clamp(m_sshLogScroll, 0.0f, maxScroll);
        }

        float ly = boxR.top + std::floor(10 * s) - m_sshLogScroll;

        if (logs.empty()) {
            m_r.Text(L"Baglanti baslatildi...", D2D1::RectF(boxR.left + lpad, ly, boxR.right - lpad, ly + std::floor(20 * s)),
                     theme::TextMuted, 11.5f * s);
        } else {
            for (const auto& l : logs) {
                if (ly + lineH >= boxR.top && ly <= boxR.bottom - std::floor(24 * s)) {
                    uint32_t col = l.color;
                    if (!col) {
                        if (l.icon == L"❗" || l.icon == L"😨") col = 0xF05454;
                        else if (l.icon == L"👤") col = 0xD8E2EC;
                        else if (l.icon == L"⚙️") col = 0x94A3B8;
                        else col = theme::TextHi;
                    }
                    const std::wstring line = l.icon + L" " + l.text;
                    m_r.Text(line, D2D1::RectF(boxR.left + lpad, ly, boxR.right - lpad, ly + lineH),
                             col, 11.0f * s, Renderer::Align::Left, false, true);
                }
                ly += lineH;
            }
        }

        if (maxScroll > 0.0f) {
            const float barTrackH = visibleH;
            const float barThumbH = std::max(std::floor(24 * s), barTrackH * (visibleH / contentH));
            const float thumbY = boxR.top + std::floor(10 * s) + (m_sshLogScroll / maxScroll) * (barTrackH - barThumbH);
            const float sbW = std::floor(4 * s);
            const D2D1_RECT_F thumbR = D2D1::RectF(boxR.right - sbW - std::floor(4 * s), thumbY, boxR.right - std::floor(4 * s), thumbY + barThumbH);
            m_r.FillRound(thumbR, sbW * 0.5f, 0x4A5568, 0.7f);
        }

        m_r.Text(L"See the Documentation to learn more about common connection issues.",
                 D2D1::RectF(boxR.left + lpad, boxR.bottom - std::floor(22 * s), boxR.right - lpad, boxR.bottom - std::floor(6 * s)),
                 theme::TextMuted, 10.5f * s);
        m_r.PopClip();
    }

    y += boxH + std::floor(16 * s);

    // 4. Alt Butonlar
    const float btnH = std::floor(36 * s);
    if (m_ui.Button(8101, D2D1::RectF(x0, y, x0 + std::floor(80 * s), y + btnH), L"Close")) {
        CloseTab(m_active);
    }

    if (m_ui.Button(8102, D2D1::RectF(x0 + std::floor(92 * s), y, x0 + std::floor(196 * s), y + btnH), L"Edit host")) {
        m_selHost = h.id;
        m_hostDetail = true;
        SetView(View::Hosts);
    }

    if (failed) {
        if (m_ui.Button(8103, D2D1::RectF(x1 - std::floor(116 * s), y, x1, y + btnH), L"Start over", true)) {
            m_sshLogScroll = -1.0f;
            std::wstring rErr;
            tab->RestartSsh(&rErr);
        }
    }
}

void MainWindow::DrawKnownHostsScreen(const D2D1_RECT_F& a) {
    const float s = m_lay.scale;
    m_r.PushClip(a);

    const float pad = std::floor(22 * s);
    const float x0 = a.left + pad;
    const float x1 = a.right - pad;
    float y = a.top + std::floor(20 * s) - m_knownHostsScroll;

    m_r.Text(Tr(Msg::KnownHostsTitle), D2D1::RectF(x0, y, x1 - std::floor(340 * s), y + std::floor(30 * s)),
             theme::TextHi, 20.0f * s, Renderer::Align::Left, true);

    const float impBtnW = std::floor(180 * s);
    const std::wstring impLabel = L"📥 " + std::wstring(Tr(Msg::KnownHostsImport));
    if (m_ui.Button(8301, D2D1::RectF(x1 - std::floor(110 * s) - impBtnW - std::floor(8 * s), y, x1 - std::floor(118 * s), y + std::floor(32 * s)),
                    impLabel.c_str(), true)) {
        size_t n = m_inv.ImportKnownHostsAndConfig(m_knownHosts);
        if (n > 0) {
            m_inv.Save(m_dataDir);
            RefreshHubNodes(false);
            m_dirty = true;
            Toast(std::to_wstring(n) + L" " + TrText(L"servers imported into inventory", L"sunucu envantere aktarıldı"));
        } else {
            Toast(TrText(L"No new servers found (already registered)", L"Yeni sunucu bulunamadı (zaten kayıtlı)"));
        }
    }

    if (m_ui.Button(8300, D2D1::RectF(x1 - std::floor(110 * s), y, x1, y + std::floor(32 * s)), Tr(Msg::ActionRefresh))) {
        m_knownHosts.Load();
        Toast(TrText(L"known_hosts file reloaded", L"known_hosts dosyasi yeniden yuklendi"));
    }
    y += std::floor(36 * s);

    std::wstring sub = TrText(L"File: ", L"Dosya: ") + m_knownHosts.GetFilePath();
    m_r.Text(sub, D2D1::RectF(x0, y, x1, y + std::floor(20 * s)), theme::TextDim, 11.5f * s);
    y += std::floor(26 * s);

    const float searchW = std::floor(320 * s);
    m_ui.Field(ID_KH_FILTER, D2D1::RectF(x0, y, x0 + searchW, y + std::floor(32 * s)),
               m_knownHostsFilter, Tr(Msg::KnownHostsFilter));
    y += std::floor(44 * s);

    const float rowH = std::floor(38 * s);
    const D2D1_RECT_F headerR = D2D1::RectF(x0, y, x1, y + rowH);
    m_r.Fill(headerR, theme::Surface);
    m_r.Stroke(headerR, theme::Border);

    const float colHostW = std::floor(180 * s);
    const float colTypeW = std::floor(110 * s);
    const float colFpW   = std::floor(280 * s);

    m_r.Text(Tr(Msg::ColHostIp), D2D1::RectF(headerR.left + std::floor(12 * s), headerR.top, headerR.left + colHostW, headerR.bottom),
             theme::TextMuted, 11.5f * s, Renderer::Align::Left, true);
    m_r.Text(Tr(Msg::ColKeyType), D2D1::RectF(headerR.left + colHostW, headerR.top, headerR.left + colHostW + colTypeW, headerR.bottom),
             theme::TextMuted, 11.5f * s, Renderer::Align::Left, true);
    m_r.Text(Tr(Msg::ColFingerprint), D2D1::RectF(headerR.left + colHostW + colTypeW, headerR.top, headerR.left + colHostW + colTypeW + colFpW, headerR.bottom),
             theme::TextMuted, 11.5f * s, Renderer::Align::Left, true);
    m_r.Text(Tr(Msg::ColActions), D2D1::RectF(headerR.left + colHostW + colTypeW + colFpW, headerR.top, headerR.right - std::floor(12 * s), headerR.bottom),
             theme::TextMuted, 11.5f * s, Renderer::Align::Left, true);

    y += rowH;

    int rendered = 0;
    const auto& entries = m_knownHosts.Entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        if (!m_knownHostsFilter.empty()) {
            if (StrStrIW(e.hostPattern.c_str(), m_knownHostsFilter.c_str()) == nullptr &&
                StrStrIW(e.fingerprint.c_str(), m_knownHostsFilter.c_str()) == nullptr &&
                StrStrIW(e.keyType.c_str(), m_knownHostsFilter.c_str()) == nullptr) {
                continue;
            }
        }

        const D2D1_RECT_F r = D2D1::RectF(x0, y, x1, y + rowH);
        if (rendered % 2 == 1) m_r.Fill(r, theme::Elevated);
        if (m_ui.Hot(r)) m_r.Fill(r, theme::Elevated);

        m_r.Text(Trunc(e.hostPattern, 24),
                 D2D1::RectF(r.left + std::floor(12 * s), r.top, r.left + colHostW, r.bottom),
                 theme::TextHi, 12.0f * s);

        uint32_t badgeCol = theme::TextMuted;
        if (e.keyType.find(L"ed25519") != std::wstring::npos) badgeCol = theme::Green;
        else if (e.keyType.find(L"ecdsa") != std::wstring::npos) badgeCol = theme::AcHi();
        else if (e.keyType.find(L"rsa") != std::wstring::npos) badgeCol = theme::Violet;

        const D2D1_RECT_F bR = D2D1::RectF(r.left + colHostW, r.top + std::floor(8 * s),
                                          r.left + colHostW + std::floor(90 * s), r.bottom - std::floor(8 * s));
        m_ui.Badge(bR, e.keyType, badgeCol);

        m_r.Text(e.fingerprint.empty() ? L"(parmak izi alinamadi)" : Trunc(e.fingerprint, 36),
                 D2D1::RectF(r.left + colHostW + colTypeW, r.top, r.left + colHostW + colTypeW + colFpW, r.bottom),
                 theme::Text, 11.5f * s);

        const float actionX = r.left + colHostW + colTypeW + colFpW;
        if (m_ui.Button(8400 + (int)i, D2D1::RectF(actionX, r.top + std::floor(6 * s), actionX + std::floor(70 * s), r.bottom - std::floor(6 * s)), Tr(Msg::MenuCopy))) {
            ClipboardSetText(m_hwnd, e.fingerprint + L" " + e.keyData);
            Toast(TrText(L"Fingerprint and key copied", L"Parmak izi ve anahtar kopyalandi"));
        }

        if (m_ui.Button(8600 + (int)i, D2D1::RectF(actionX + std::floor(78 * s), r.top + std::floor(6 * s), actionX + std::floor(128 * s), r.bottom - std::floor(6 * s)), Tr(Msg::SftpMenuDelete), false, true)) {
            m_knownHosts.DeleteEntry(i);
            Toast(TrText(L"Host key deleted", L"Host anahtari silindi"));
            break;
        }

        m_r.Line(r.left, r.bottom, r.right, r.bottom, theme::Border, 1.0f);
        y += rowH;
        ++rendered;
    }

    if (rendered == 0) {
        m_r.Text(Tr(Msg::KnownHostsEmpty),
                 D2D1::RectF(x0, y + std::floor(16 * s), x1, y + std::floor(40 * s)),
                 theme::TextMuted, 12.5f * s);
    }

    m_r.PopClip();
}

void MainWindow::DrawSftpScreen(const D2D1_RECT_F& a, SftpController* ctrl) {
    if (!ctrl && !m_sftp) m_sftp = std::make_unique<SftpController>(m_inv);
    auto* sftp = ctrl ? ctrl : m_sftp.get();
    const float s = m_lay.scale;
    const bool isTr = (I18n::CurrentLang() == LangId::Tr);
    m_r.Fill(a, theme::Base); // SFTP ekrani ve sekmeleri daima %100 opak (solid)
    m_r.PushClip(a);

    // Transfer bildirim ve liste yenileme dinleyicisini bağla
    sftp->SetOnStateChanged([this]() {
        if (m_hwnd) {
            m_dirty = true;
            PostMessageW(m_hwnd, WM_USER + 777, 0, 0);
        }
    });

    float topY = a.top;

    // ==========================================
    // TRANSFER HUD BANNER (Upload & Download Progress)
    // ==========================================
    if (sftp->HasTransferBanner()) {
        const float bannerH = std::floor(36 * s);
        const D2D1_RECT_F banR = D2D1::RectF(a.left + std::floor(10 * s), topY + std::floor(6 * s),
                                            a.right - std::floor(10 * s), topY + bannerH + std::floor(2 * s));
        
        auto tf = sftp->CurrentTransfer();
        const bool isUp = (tf.kind == TransferKind::Upload);

        uint32_t bgCol = theme::Elevated;
        uint32_t borderCol = theme::Ac();
        uint32_t textCol = theme::TextHi;
        uint32_t badgeCol = isUp ? theme::AcHi() : 0x50fa7b;

        if (tf.status == TransferStatus::Failed) {
            borderCol = 0xff5555;
            badgeCol = 0xff5555;
        } else if (tf.status == TransferStatus::Completed) {
            borderCol = 0x50fa7b;
            badgeCol = 0x50fa7b;
        }

        m_r.FillRound(banR, 6 * s, bgCol);
        m_r.StrokeRound(banR, 6 * s, borderCol, 1.2f);

        // Rozet / Badge: [ 📤 YÜKLEME ] veya [ 📥 İNDİRME ]
        const float badgeW = std::floor(106 * s);
        const D2D1_RECT_F badgeR = D2D1::RectF(banR.left + std::floor(8 * s), banR.top + std::floor(5 * s),
                                              banR.left + std::floor(8 * s) + badgeW, banR.bottom - std::floor(5 * s));
        m_r.FillRound(badgeR, 4 * s, theme::Surface);
        m_r.StrokeRound(badgeR, 4 * s, badgeCol, 1.0f);

        std::wstring badgeText = isUp ? Tr(Msg::SftpUploadBadge) : Tr(Msg::SftpDownloadBadge);
        m_r.Text(badgeText, badgeR, badgeCol, 10.0f * s, Renderer::Align::Center, true);

        // Metin ve İlerleme
        const float closeBtnW = std::floor(26 * s);
        const float progW = (tf.status == TransferStatus::InProgress) ? std::floor(120 * s) : 0.0f;
        const float textLeft = badgeR.right + std::floor(10 * s);
        const float textRight = banR.right - closeBtnW - progW - std::floor(16 * s);

        std::wstring dispText = tf.fileName;
        if (!tf.statusText.empty()) {
            dispText += L"  •  " + tf.statusText;
        }
        m_r.Text(Trunc(dispText, 70), D2D1::RectF(textLeft, banR.top + std::floor(5 * s), textRight, banR.bottom - std::floor(5 * s)),
                 textCol, 11.5f * s, Renderer::Align::Left, false, true);

        if (tf.status == TransferStatus::InProgress) {
            const float progH = std::floor(10 * s);
            const D2D1_RECT_F progBox = D2D1::RectF(textRight + std::floor(10 * s),
                                                   banR.top + std::floor(12 * s),
                                                   textRight + std::floor(10 * s) + progW,
                                                   banR.top + std::floor(12 * s) + progH);
            m_r.FillRound(progBox, 3 * s, theme::Surface);
            m_r.StrokeRound(progBox, 3 * s, theme::Border, 0.8f);

            int pct = std::clamp(tf.progressPercent, 0, 100);
            if (pct > 0) {
                const float filledW = (progBox.right - progBox.left) * (pct / 100.0f);
                const D2D1_RECT_F fillR = D2D1::RectF(progBox.left, progBox.top, progBox.left + filledW, progBox.bottom);
                m_r.FillRound(fillR, 3 * s, badgeCol);
            }
        }

        // Kapatma butonu [✕]
        const D2D1_RECT_F closeR = D2D1::RectF(banR.right - closeBtnW - std::floor(4 * s), banR.top + std::floor(5 * s),
                                              banR.right - std::floor(6 * s), banR.bottom - std::floor(5 * s));
        if (m_ui.Button(9099, closeR, L"✕")) {
            sftp->DismissTransferBanner();
        }

        topY += bannerH + std::floor(8 * s);
    }

    const float mid = std::floor((a.left + a.right) * 0.5f);
    const D2D1_RECT_F leftA = D2D1::RectF(a.left, topY, mid - 1, a.bottom - std::floor(26 * s));
    const D2D1_RECT_F rightA = D2D1::RectF(mid + 1, topY, a.right, a.bottom - std::floor(26 * s));
    m_r.Line(mid, topY, mid, a.bottom - std::floor(26 * s), theme::Border, 1.0f);

    // ==========================================
    // SOL PANEL: Yerel Dosya Gezgini (Dual-Pane Local)
    // ==========================================
    {
        const float pad = std::floor(14 * s);
        float y = leftA.top + std::floor(10 * s);

        const D2D1_RECT_F locIc = D2D1::RectF(leftA.left + pad, y + 2 * s, leftA.left + pad + 20 * s, y + 22 * s);
        DrawIcon(Icon::Folder, locIc, theme::AcHi());
        const std::wstring locTitle = TrText(L"Local Files", L"Yerel Dosyalar");
        m_r.Text(locTitle, D2D1::RectF(locIc.right + 8 * s, y, leftA.left + std::floor(180 * s), y + 26 * s),
                 theme::TextHi, 14.5f * s, Renderer::Align::Left, true);

        // Hızlı Sürücü Değiştirme Butonları (C:, D:, E:)
        auto drives = LocalFileSystem::GetDrives();
        float drvX = leftA.left + std::floor(190 * s);
        for (size_t di = 0; di < drives.size() && di < 4; ++di) {
            std::wstring dLabel = drives[di];
            if (dLabel.size() >= 2) dLabel = dLabel.substr(0, 2);
            const float drvW = std::floor(30 * s);
            if (drvX + drvW < leftA.right - std::floor(220 * s)) {
                if (m_ui.Button(9020 + (int)di, D2D1::RectF(drvX, y, drvX + drvW, y + std::floor(26 * s)), dLabel.c_str())) {
                    sftp->SetLocalPath(drives[di]);
                    m_sftpLocalScroll = 0.0f;
                }
                drvX += drvW + std::floor(4 * s);
            }
        }

        const float filW = std::floor(130 * s);
        m_ui.Field(ID_SFTP_LOCAL_FILTER, D2D1::RectF(leftA.right - pad - filW - std::floor(68 * s), y, leftA.right - pad - std::floor(68 * s), y + std::floor(26 * s)),
                   m_sftpLocalFilter, TrText(L"Filter...", L"Filtre..."));

        if (m_ui.Button(9001, D2D1::RectF(leftA.right - pad - std::floor(62 * s), y, leftA.right - pad, y + std::floor(26 * s)), Tr(Msg::ActionRefresh))) {
            sftp->RefreshLocal();
        }
        y += std::floor(32 * s);

        // Adres ve Gezinme Çubuğu
        const float upBtnW = std::floor(96 * s);
        const std::wstring upLabel = L"⬆️ " + std::wstring(TrText(L"Parent", L"Üst Dizin"));
        if (m_ui.Button(9002, D2D1::RectF(leftA.left + pad, y, leftA.left + pad + upBtnW, y + std::floor(24 * s)), upLabel.c_str())) {
            sftp->LocalNavigateUp();
            m_sftpLocalScroll = 0.0f;
        }

        std::wstring lPathDisp = sftp->LocalPath();
        const D2D1_RECT_F pathBox = D2D1::RectF(leftA.left + pad + upBtnW + std::floor(6 * s), y, leftA.right - pad - std::floor(60 * s), y + std::floor(24 * s));
        m_r.FillRound(pathBox, 4 * s, theme::Surface);
        m_r.Stroke(pathBox, theme::Border);
        m_r.Text(Trunc(lPathDisp, 48), D2D1::RectF(pathBox.left + 6 * s, pathBox.top, pathBox.right - 6 * s, pathBox.bottom),
                 theme::AcHi(), 11.5f * s, Renderer::Align::Left, false, true);

        if (m_ui.Button(9003, D2D1::RectF(leftA.right - pad - std::floor(54 * s), y, leftA.right - pad, y + std::floor(24 * s)), Tr(Msg::MenuCopy))) {
            ClipboardSetText(m_hwnd, lPathDisp);
            Toast(TrText(L"Local path copied", L"Yerel yol kopyalandı"));
        }
        y += std::floor(30 * s);

        // Aksiyon Çubuğu: ⬆️ Yükle, + Klasör, Sil (Kullanıcı talebi: Yükle yerel tarafa alındı!)
        const float actH = std::floor(24 * s);
        const bool canUpload = !m_sftpSelLocal.empty() && m_sftpSelLocal != L".." &&
                               sftp->RemoteState() == SftpConnectionState::Connected;
        const std::wstring upAction = L"⬆️ " + std::wstring(Tr(Msg::SftpUploadAction)) + L" ->";
        const std::wstring newFolderAction = L"+ " + std::wstring(TrText(L"Folder", L"Klasör"));
        if (m_ui.Button(9010, D2D1::RectF(leftA.left + pad, y, leftA.left + pad + std::floor(100 * s), y + actH),
                        upAction.c_str(), canUpload, true)) {
            std::wstring err;
            sftp->UploadSelected(m_sftpSelLocal, &err);
        }
        if (m_ui.Button(9004, D2D1::RectF(leftA.left + pad + std::floor(106 * s), y, leftA.left + pad + std::floor(186 * s), y + actH), newFolderAction.c_str())) {
            m_sftpNewFolderPrompt = true;
            m_sftpNewFolderIsRemote = false;
            m_sftpNewFolderName.clear();
        }
        if (m_ui.Button(9005, D2D1::RectF(leftA.left + pad + std::floor(192 * s), y, leftA.left + pad + std::floor(252 * s), y + actH),
                        Tr(Msg::SftpMenuDelete), !m_sftpSelLocal.empty() && m_sftpSelLocal != L"..", true)) {
            std::wstring err;
            sftp->DeleteLocalItem(m_sftpSelLocal, false, &err);
            m_sftpSelLocal.clear();
        }
        y += actH + std::floor(8 * s);

        // Tablo Başlıkları
        const float rowH = std::floor(28 * s);
        const D2D1_RECT_F headR = D2D1::RectF(leftA.left + pad, y, leftA.right - pad, y + rowH);
        m_r.Fill(headR, theme::Surface);
        m_r.Stroke(headR, theme::Border);

        const float colNameW = (headR.right - headR.left) * 0.44f;
        const float colDateW = (headR.right - headR.left) * 0.28f;
        const float colSizeW = (headR.right - headR.left) * 0.14f;

        m_r.Text(Tr(Msg::ColName), D2D1::RectF(headR.left + 8 * s, headR.top, headR.left + colNameW, headR.bottom),
                 theme::TextMuted, 11.0f * s, Renderer::Align::Left, true);
        m_r.Text(Tr(Msg::ColDate), D2D1::RectF(headR.left + colNameW, headR.top, headR.left + colNameW + colDateW, headR.bottom),
                 theme::TextMuted, 11.0f * s, Renderer::Align::Left, true);
        m_r.Text(Tr(Msg::ColSize), D2D1::RectF(headR.left + colNameW + colDateW, headR.top, headR.left + colNameW + colDateW + colSizeW, headR.bottom),
                 theme::TextMuted, 11.0f * s, Renderer::Align::Left, true);
        m_r.Text(Tr(Msg::ColType), D2D1::RectF(headR.left + colNameW + colDateW + colSizeW, headR.top, headR.right - 8 * s, headR.bottom),
                 theme::TextMuted, 11.0f * s, Renderer::Align::Left, true);

        y += rowH;

        // Scroll edilebilir liste
        const auto items = sftp->GetFilteredLocal(m_sftpLocalFilter);
        const float listTop = y;
        const float listBottom = leftA.bottom - std::floor(10 * s);
        const float viewH = std::max(1.0f, listBottom - listTop);
        const float totalH = items.size() * rowH;
        const float maxScroll = std::max(0.0f, totalH - viewH);
        m_sftpLocalScroll = std::clamp(m_sftpLocalScroll, 0.0f, maxScroll);

        const D2D1_RECT_F listClip = D2D1::RectF(leftA.left + pad, listTop, leftA.right - pad, listBottom);
        m_r.Fill(listClip, theme::Sunken);
        m_r.Stroke(listClip, theme::Border);
        m_r.PushClip(listClip);

        for (size_t i = 0; i < items.size(); ++i) {
            float rowY = listTop - m_sftpLocalScroll + i * rowH;
            if (rowY + rowH < listTop) continue;
            if (rowY > listBottom) break;

            const auto& item = items[i];
            const D2D1_RECT_F r = D2D1::RectF(leftA.left + pad, rowY, leftA.right - pad - (maxScroll > 0 ? std::floor(8 * s) : 0), rowY + rowH);

            const bool selected = (m_sftpSelLocal == item.name);
            if (selected) m_r.Fill(r, theme::Elevated);
            else if (m_ui.Hot(r)) m_r.Fill(r, theme::Surface);

            const bool isDbl = DblClickIn(r);
            if (ClickIn(r) || isDbl) {
                m_sftpSelLocal = item.name;
                if (isDbl || item.name == L"..") {
                    if (item.isDir) {
                        if (item.name == L"..") sftp->LocalNavigateUp();
                        else sftp->LocalNavigateDown(item.name);
                        m_sftpLocalScroll = 0.0f;
                    } else {
                        std::wstring fullPath = sftp->LocalPath();
                        if (fullPath.back() != L'\\') fullPath += L'\\';
                        fullPath += item.name;
                        ShellExecuteW(nullptr, L"open", fullPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    }
                }
            }

            const D2D1_RECT_F ic = D2D1::RectF(r.left + 6 * s, r.top + 5 * s, r.left + 22 * s, r.bottom - 5 * s);
            DrawIcon(item.isDir ? Icon::Folder : Icon::Terminal, ic, item.isDir ? theme::Amber : theme::TextDim);

            m_r.Text(Trunc(item.name, 32), D2D1::RectF(r.left + 28 * s, r.top, r.left + colNameW, r.bottom),
                     item.isDir ? theme::TextHi : theme::Text, 11.5f * s);
            m_r.Text(item.modified, D2D1::RectF(r.left + colNameW, r.top, r.left + colNameW + colDateW, r.bottom),
                     theme::TextDim, 10.5f * s);
            m_r.Text(item.isDir ? L"--" : LocalFileSystem::FormatFileSize(item.size),
                     D2D1::RectF(r.left + colNameW + colDateW, r.top, r.left + colNameW + colDateW + colSizeW, r.bottom),
                     theme::TextDim, 10.5f * s);
            m_r.Text(item.kind, D2D1::RectF(r.left + colNameW + colDateW + colSizeW, r.top, r.right - 8 * s, r.bottom),
                     theme::TextDim, 10.5f * s);

            m_r.Line(r.left, r.bottom, r.right, r.bottom, theme::Border, 0.5f);
        }

        m_r.PopClip();

        // Scrollbar (Yerel)
        if (maxScroll > 0.0f) {
            const float sbW = std::floor(5 * s);
            const float sbX = leftA.right - pad - sbW;
            const float thumbH = std::max(std::floor(24 * s), (viewH / totalH) * viewH);
            const float thumbY = listTop + (m_sftpLocalScroll / maxScroll) * (viewH - thumbH);
            m_r.FillRound(D2D1::RectF(sbX, thumbY, sbX + sbW, thumbY + thumbH), sbW * 0.5f, theme::BorderHi);
        }
    }

    // ==========================================
    // SAĞ PANEL: Uzak SFTP Gezgini (Dual-Pane Remote)
    // ==========================================
    {
        const float pad = std::floor(14 * s);

        if (m_sftpSelectingHost || sftp->RemoteState() == SftpConnectionState::Disconnected) {
            float y = rightA.top + std::floor(10 * s);
            m_r.Text(Tr(Msg::SftpSelectHostTitle), D2D1::RectF(rightA.left + pad, y, rightA.right - pad, y + std::floor(26 * s)),
                     theme::TextHi, 15.0f * s, Renderer::Align::Left, true);
            y += std::floor(30 * s);

            m_ui.Field(ID_SFTP_HOST_SEARCH, D2D1::RectF(rightA.left + pad, y, rightA.right - pad, y + std::floor(30 * s)),
                       m_sftpSearchHost, Tr(Msg::SftpSearchHost));
            y += std::floor(38 * s);

            m_r.Text(Tr(Msg::SftpSavedServers), D2D1::RectF(rightA.left + pad, y, rightA.right - pad, y + std::floor(20 * s)),
                     theme::TextMuted, 11.5f * s, Renderer::Align::Left, true);
            y += std::floor(24 * s);

            const auto& allHosts = m_inv.hosts();
            std::vector<Host> filteredHosts;
            for (const auto& h : allHosts) {
                if (!m_sftpSearchHost.empty()) {
                    if (StrStrIW(h.Display().c_str(), m_sftpSearchHost.c_str()) == nullptr &&
                        StrStrIW(h.address.c_str(), m_sftpSearchHost.c_str()) == nullptr) {
                        continue;
                    }
                }
                filteredHosts.push_back(h);
            }

            if (filteredHosts.empty()) {
                m_r.Text(allHosts.empty() ? Tr(Msg::SftpNoHostsConfigured)
                                          : Tr(Msg::SftpNoHostsFound),
                         D2D1::RectF(rightA.left + pad, y + std::floor(20 * s), rightA.right - pad, y + std::floor(80 * s)),
                         theme::TextDim, 12.0f * s, Renderer::Align::Center);
            } else {
                const float listTop = y;
                const float listBottom = rightA.bottom - pad;
                const float viewH = std::max(1.0f, listBottom - listTop);
                const float cardH = std::floor(50 * s);
                const float totalH = filteredHosts.size() * (cardH + std::floor(6 * s));
                const float maxScroll = std::max(0.0f, totalH - viewH);
                m_sftpRemoteScroll = std::clamp(m_sftpRemoteScroll, 0.0f, maxScroll);

                m_r.PushClip(D2D1::RectF(rightA.left + pad, listTop, rightA.right - pad, listBottom));
                float cy = listTop - m_sftpRemoteScroll;

                for (size_t i = 0; i < filteredHosts.size(); ++i) {
                    const auto& h = filteredHosts[i];
                    if (cy + cardH >= listTop && cy <= listBottom) {
                        const D2D1_RECT_F r = D2D1::RectF(rightA.left + pad, cy, rightA.right - pad - (maxScroll > 0 ? std::floor(10 * s) : 0), cy + cardH);
                        const bool hot = m_ui.Hot(r);
                        if (hot) m_r.FillRound(r, std::floor(6 * s), theme::Elevated);
                        else m_r.FillRound(r, std::floor(6 * s), theme::Surface);
                        m_r.Stroke(r, hot ? theme::BorderHi : theme::Border);

                        const D2D1_RECT_F ic = D2D1::RectF(r.left + 8 * s, r.top + 7 * s, r.left + 40 * s, r.bottom - 7 * s);
                        m_r.FillRound(ic, 6 * s, h.production ? theme::Red : 0xE95420);
                        DrawIcon(Icon::Terminal, D2D1::RectF(ic.left + 6 * s, ic.top + 6 * s, ic.right - 6 * s, ic.bottom - 6 * s), 0xFFFFFF);

                        const float btnAreaW = (ctrl != nullptr) ? std::floor(154 * s) : std::floor(124 * s);
                        const float textR = r.right - btnAreaW - std::floor(8 * s);

                        m_r.Text(h.Display(), D2D1::RectF(r.left + 48 * s, r.top + 6 * s, textR, r.top + 24 * s),
                                 theme::TextHi, 12.5f * s, Renderer::Align::Left, true);
                        std::wstring sub = (h.username.empty() ? L"root" : h.username) + L"@" + h.address + L":" + std::to_wstring(h.port ? h.port : 22);
                        m_r.Text(sub, D2D1::RectF(r.left + 48 * s, r.top + 24 * s, textR, r.bottom - 4 * s),
                                 theme::TextDim, 10.5f * s);

                        const float btnH = std::floor(26 * s);
                        const float btnY = r.top + (cardH - btnH) * 0.5f;

                        if (ctrl != nullptr) {
                            // Aktif SFTP sekmesindeyiz: Bu sekmede baglan veya Yeni sekmede ac
                            const float b1W = std::floor(70 * s), b2W = std::floor(76 * s);
                            const D2D1_RECT_F bNew = D2D1::RectF(r.right - b2W - std::floor(8 * s), btnY, r.right - std::floor(8 * s), btnY + btnH);
                            const D2D1_RECT_F bHere = D2D1::RectF(bNew.left - b1W - std::floor(6 * s), btnY, bNew.left - std::floor(6 * s), btnY + btnH);

                            if (m_ui.Button(9200 + (int)i * 2, bHere, Tr(Msg::SftpConnectButton), hot)) {
                                sftp->ConnectRemote(h);
                                m_sftpSelectingHost = false;
                                m_sftpRemoteScroll = 0.0f;
                            }
                            if (m_ui.Button(9200 + (int)i * 2 + 1, bNew, Tr(Msg::SftpOpenNewTab))) {
                                NewSftpTab(&h);
                            }
                        } else {
                            // Ana menudeki SFTP ekranindayiz: Yeni sekmede acar
                            const float bW = std::floor(116 * s);
                            const D2D1_RECT_F bNew = D2D1::RectF(r.right - bW - std::floor(8 * s), btnY, r.right - std::floor(8 * s), btnY + btnH);

                            if (m_ui.Button(9200 + (int)i, bNew, Tr(Msg::SftpOpenNewTab), hot)) {
                                NewSftpTab(&h);
                            }
                        }

                        if (ClickIn(r) && !m_ui.Hot(D2D1::RectF(r.right - btnAreaW - std::floor(10 * s), r.top, r.right, r.bottom))) {
                            if (ctrl != nullptr) {
                                sftp->ConnectRemote(h);
                                m_sftpSelectingHost = false;
                                m_sftpRemoteScroll = 0.0f;
                            } else {
                                NewSftpTab(&h);
                            }
                        }
                    }
                    cy += cardH + std::floor(6 * s);
                }

                m_r.PopClip();

                if (maxScroll > 0.0f) {
                    const float sbW = std::floor(5 * s);
                    const float sbX = rightA.right - pad - sbW;
                    const float thumbH = std::max(std::floor(24 * s), (viewH / totalH) * viewH);
                    const float thumbY = listTop + (m_sftpRemoteScroll / maxScroll) * (viewH - thumbH);
                    m_r.FillRound(D2D1::RectF(sbX, thumbY, sbX + sbW, thumbY + thumbH), sbW * 0.5f, theme::BorderHi);
                }
            }
        }

        else if (sftp->RemoteState() == SftpConnectionState::Connecting) {
            const float cx = std::floor((rightA.left + rightA.right) * 0.5f);
            const float cy = std::floor((rightA.top + rightA.bottom) * 0.5f);

            const float spW = std::floor(220 * s), spH = std::floor(36 * s);
            const D2D1_RECT_F spBox = D2D1::RectF(cx - spW * 0.5f, cy - spH * 0.5f, cx + spW * 0.5f, cy + spH * 0.5f);
            m_r.FillRound(spBox, 8 * s, theme::Surface);
            m_r.Stroke(spBox, theme::BorderHi);

            m_r.Text(Tr(Msg::SftpConnecting), spBox, theme::AcHi(), 13.5f * s, Renderer::Align::Center, true);
        }
        else if (sftp->RemoteState() == SftpConnectionState::Failed) {
            const float cx = std::floor((rightA.left + rightA.right) * 0.5f);
            const float cy = std::floor((rightA.top + rightA.bottom) * 0.5f);
            m_r.Text(Tr(Msg::SftpConnectionFailed), D2D1::RectF(rightA.left, cy - 60 * s, rightA.right, cy - 36 * s),
                     theme::Red, 15.0f * s, Renderer::Align::Center, true);
            m_r.Text(sftp->RemoteError(), D2D1::RectF(rightA.left + pad, cy - 30 * s, rightA.right - pad, cy + 60 * s),
                     theme::TextMuted, 11.0f * s, Renderer::Align::Center, false, false, 1.0f, true);

            if (m_ui.Button(9301, D2D1::RectF(cx - 70 * s, cy + 70 * s, cx + 70 * s, cy + 106 * s), Tr(Msg::SftpSelectAnotherServer), true)) {
                sftp->DisconnectRemote();
                m_sftpSelectingHost = true;
            }

        }
        else if (sftp->RemoteState() == SftpConnectionState::Connected) {
            float y = rightA.top + std::floor(10 * s);

            const auto* rh = sftp->ConnectedHost();
            const std::wstring hostName = rh ? rh->Display() : std::wstring(Tr(Msg::SftpRemoteServer));
            m_r.Disc(rightA.left + pad + 6 * s, y + 11 * s, 4 * s, theme::Green);
            m_r.Text(hostName, D2D1::RectF(rightA.left + pad + 16 * s, y, rightA.left + std::floor(200 * s), y + 24 * s),
                     theme::TextHi, 14.0f * s, Renderer::Align::Left, true);

            const float filW = std::floor(120 * s);
            m_ui.Field(ID_SFTP_REMOTE_FILTER, D2D1::RectF(rightA.right - pad - filW - std::floor(160 * s), y, rightA.right - pad - std::floor(160 * s), y + std::floor(26 * s)),
                       m_sftpRemoteFilter, TrText(L"Filter...", L"Filtre..."));

            if (m_ui.Button(9401, D2D1::RectF(rightA.right - pad - std::floor(150 * s), y, rightA.right - pad - std::floor(92 * s), y + std::floor(26 * s)), Tr(Msg::ActionRefresh))) {
                sftp->RefreshRemote();
            }

            if (m_ui.Button(9400, D2D1::RectF(rightA.right - pad - std::floor(86 * s), y, rightA.right - pad, y + std::floor(26 * s)), Tr(Msg::ActionDisconnect))) {
                sftp->DisconnectRemote();
            }
            y += std::floor(32 * s);

            // Adres ve Gezinme Çubuğu
            const float upBtnW = std::floor(86 * s);
            const std::wstring upRemoteLabel = L"⬆️ " + std::wstring(TrText(L"Parent", L"Üst Dizin"));
            if (m_ui.Button(9402, D2D1::RectF(rightA.left + pad, y, rightA.left + pad + upBtnW, y + std::floor(24 * s)), upRemoteLabel.c_str())) {
                sftp->RemoteNavigateUp();
                m_sftpRemoteScroll = 0.0f;
            }

            std::wstring rPathDisp = sftp->RemotePath();
            const D2D1_RECT_F pathBox = D2D1::RectF(rightA.left + pad + upBtnW + std::floor(6 * s), y, rightA.right - pad - std::floor(60 * s), y + std::floor(24 * s));
            m_r.FillRound(pathBox, 4 * s, theme::Surface);
            m_r.Stroke(pathBox, theme::Border);
            m_r.Text(Trunc(rPathDisp, 48), D2D1::RectF(pathBox.left + 6 * s, pathBox.top, pathBox.right - 6 * s, pathBox.bottom),
                     theme::AcHi(), 11.5f * s, Renderer::Align::Left, false, true);

            if (m_ui.Button(9403, D2D1::RectF(rightA.right - pad - std::floor(54 * s), y, rightA.right - pad, y + std::floor(24 * s)), Tr(Msg::MenuCopy))) {
                ClipboardSetText(m_hwnd, rPathDisp);
                Toast(TrText(L"Remote path copied", L"Uzak yol kopyalandı"));
            }
            y += std::floor(30 * s);

            // Aksiyon Çubuğu: <- ⬇️ İndir, + Klasör, Sil
            const float abtnH = std::floor(24 * s);
            const bool canDownload = !m_sftpSelRemote.empty() && m_sftpSelRemote != L".." &&
                                     sftp->RemoteState() == SftpConnectionState::Connected;
            const std::wstring dlAction = L"<- ⬇️ " + std::wstring(Tr(Msg::SftpDownloadAction));
            const std::wstring newFolderRemote = L"+ " + std::wstring(TrText(L"Folder", L"Klasör"));
            if (m_ui.Button(9411, D2D1::RectF(rightA.left + pad, y, rightA.left + pad + std::floor(100 * s), y + abtnH),
                            dlAction.c_str(), canDownload, true)) {
                std::wstring err;
                sftp->DownloadSelected(m_sftpSelRemote, &err);
            }
            if (m_ui.Button(9413, D2D1::RectF(rightA.left + pad + std::floor(106 * s), y, rightA.left + pad + std::floor(186 * s), y + abtnH), newFolderRemote.c_str())) {
                m_sftpNewFolderPrompt = true;
                m_sftpNewFolderIsRemote = true;
                m_sftpNewFolderName.clear();
            }
            if (m_ui.Button(9412, D2D1::RectF(rightA.left + pad + std::floor(192 * s), y, rightA.left + pad + std::floor(252 * s), y + abtnH),
                            Tr(Msg::SftpMenuDelete), !m_sftpSelRemote.empty() && m_sftpSelRemote != L"..", true)) {
                std::wstring err;
                sftp->DeleteRemoteItem(m_sftpSelRemote, false, &err);
                m_sftpSelRemote.clear();
            }
            y += abtnH + std::floor(8 * s);

            // Tablo Başlıkları
            const float rowH = std::floor(28 * s);
            const D2D1_RECT_F headR = D2D1::RectF(rightA.left + pad, y, rightA.right - pad, y + rowH);
            m_r.Fill(headR, theme::Surface);
            m_r.Stroke(headR, theme::Border);

            const float colNameW = (headR.right - headR.left) * 0.44f;
            const float colDateW = (headR.right - headR.left) * 0.28f;
            const float colSizeW = (headR.right - headR.left) * 0.14f;

            m_r.Text(Tr(Msg::ColName), D2D1::RectF(headR.left + 8 * s, headR.top, headR.left + colNameW, headR.bottom),
                     theme::TextMuted, 11.0f * s, Renderer::Align::Left, true);
            m_r.Text(Tr(Msg::ColDate), D2D1::RectF(headR.left + colNameW, headR.top, headR.left + colNameW + colDateW, headR.bottom),
                     theme::TextMuted, 11.0f * s, Renderer::Align::Left, true);
            m_r.Text(Tr(Msg::ColSize), D2D1::RectF(headR.left + colNameW + colDateW, headR.top, headR.left + colNameW + colDateW + colSizeW, headR.bottom),
                     theme::TextMuted, 11.0f * s, Renderer::Align::Left, true);
            m_r.Text(Tr(Msg::ColPermissions), D2D1::RectF(headR.left + colNameW + colDateW + colSizeW, headR.top, headR.right - 8 * s, headR.bottom),
                     theme::TextMuted, 11.0f * s, Renderer::Align::Left, true);

            y += rowH;

            // Scroll edilebilir liste (Uzak)
            const auto items = sftp->GetFilteredRemote(m_sftpRemoteFilter);
            const float listTop = y;
            const float listBottom = rightA.bottom - std::floor(10 * s);
            const float viewH = std::max(1.0f, listBottom - listTop);
            const float totalH = items.size() * rowH;
            const float maxScroll = std::max(0.0f, totalH - viewH);
            m_sftpRemoteScroll = std::clamp(m_sftpRemoteScroll, 0.0f, maxScroll);

            const D2D1_RECT_F listClip = D2D1::RectF(rightA.left + pad, listTop, rightA.right - pad, listBottom);
            m_r.Fill(listClip, theme::Sunken);
            m_r.Stroke(listClip, theme::Border);
            m_r.PushClip(listClip);

            for (size_t i = 0; i < items.size(); ++i) {
                float rowY = listTop - m_sftpRemoteScroll + i * rowH;
                if (rowY + rowH < listTop) continue;
                if (rowY > listBottom) break;

                const auto& item = items[i];
                const D2D1_RECT_F r = D2D1::RectF(rightA.left + pad, rowY, rightA.right - pad - (maxScroll > 0 ? std::floor(8 * s) : 0), rowY + rowH);

                const bool selected = (m_sftpSelRemote == item.name);
                if (selected) m_r.Fill(r, theme::Elevated);
                else if (m_ui.Hot(r)) m_r.Fill(r, theme::Surface);

                const bool isDbl = DblClickIn(r);
                if (ClickIn(r) || isDbl) {
                    m_sftpSelRemote = item.name;
                    if (isDbl || item.name == L"..") {
                        if (item.isDir) {
                            if (item.name == L"..") sftp->RemoteNavigateUp();
                            else sftp->RemoteNavigateDown(item.name);
                            m_sftpRemoteScroll = 0.0f;
                        } else {
                            OpenRemoteFile(item.name);
                        }
                    }
                }

                const D2D1_RECT_F ic = D2D1::RectF(r.left + 6 * s, r.top + 5 * s, r.left + 22 * s, r.bottom - 5 * s);
                DrawIcon(item.isDir ? Icon::Folder : Icon::Terminal, ic, item.isDir ? theme::Amber : theme::TextDim);

                m_r.Text(Trunc(item.name, 32), D2D1::RectF(r.left + 28 * s, r.top, r.left + colNameW, r.bottom),
                         item.isDir ? theme::TextHi : theme::Text, 11.5f * s);
                m_r.Text(item.modified, D2D1::RectF(r.left + colNameW, r.top, r.left + colNameW + colDateW, r.bottom),
                         theme::TextDim, 10.5f * s);
                m_r.Text(item.isDir ? L"--" : LocalFileSystem::FormatFileSize(item.size),
                         D2D1::RectF(r.left + colNameW + colDateW, r.top, r.left + colNameW + colDateW + colSizeW, r.bottom),
                         theme::TextDim, 10.5f * s);
                std::wstring kindOrPerm = item.permissions.empty() ? item.kind : item.permissions;
                m_r.Text(kindOrPerm, D2D1::RectF(r.left + colNameW + colDateW + colSizeW, r.top, r.right - 8 * s, r.bottom),
                         theme::TextDim, 10.5f * s);

                m_r.Line(r.left, r.bottom, r.right, r.bottom, theme::Border, 0.5f);
            }

            m_r.PopClip();

            // Scrollbar (Uzak)
            if (maxScroll > 0.0f) {
                const float sbW = std::floor(5 * s);
                const float sbX = rightA.right - pad - sbW;
                const float thumbH = std::max(std::floor(24 * s), (viewH / totalH) * viewH);
                const float thumbY = listTop + (m_sftpRemoteScroll / maxScroll) * (viewH - thumbH);
                m_r.FillRound(D2D1::RectF(sbX, thumbY, sbX + sbW, thumbY + thumbH), sbW * 0.5f, theme::BorderHi);
            }
        }
    }

    // Alt Durum Çubuğu
    const D2D1_RECT_F stBar = D2D1::RectF(a.left, a.bottom - std::floor(24 * s), a.right, a.bottom);
    m_r.Fill(stBar, theme::Surface);
    m_r.Line(stBar.left, stBar.top, stBar.right, stBar.top, theme::Border, 1.0f);
    m_r.Text(sftp->StatusMessage().empty() ? Tr(Msg::SftpStatusReady) : sftp->StatusMessage(),
             D2D1::RectF(stBar.left + std::floor(14 * s), stBar.top, stBar.right - std::floor(14 * s), stBar.bottom),
             theme::AcHi(), 11.0f * s, Renderer::Align::Left);

    // Yeni Klasör Oluşturma Modal Dialogu
    if (m_sftpNewFolderPrompt) {
        m_r.Fill(a, 0x90000000); // karartma

        const float dw = std::floor(360 * s), dh = std::floor(140 * s);
        const float dx = std::floor((a.left + a.right) * 0.5f - dw * 0.5f);
        const float dy = std::floor((a.top + a.bottom) * 0.5f - dh * 0.5f);
        const D2D1_RECT_F dR = D2D1::RectF(dx, dy, dx + dw, dy + dh);

        m_r.FillRound(dR, 8 * s, theme::Surface);
        m_r.Stroke(dR, theme::AcHi());

        std::wstring title = Tr(Msg::SftpNewFolderTitle);
        m_r.Text(title, D2D1::RectF(dx + 16 * s, dy + 14 * s, dx + dw - 16 * s, dy + 34 * s), theme::TextHi, 13.5f * s, Renderer::Align::Left, true);

        m_ui.Field(ID_SFTP_NEW_FOLDER, D2D1::RectF(dx + 16 * s, dy + 44 * s, dx + dw - 16 * s, dy + 76 * s), m_sftpNewFolderName, Tr(Msg::SftpFolderPlaceholder));

        if (m_ui.Button(9090, D2D1::RectF(dx + dw - std::floor(170 * s), dy + std::floor(90 * s), dx + dw - std::floor(96 * s), dy + std::floor(122 * s)), Tr(Msg::ActionCancel))) {
            m_sftpNewFolderPrompt = false;
        }

        if (m_ui.Button(9091, D2D1::RectF(dx + dw - std::floor(86 * s), dy + std::floor(90 * s), dx + dw - 16 * s, dy + std::floor(122 * s)), Tr(Msg::ActionCreate), !m_sftpNewFolderName.empty())) {
            std::wstring err;
            if (m_sftpNewFolderIsRemote) {
                sftp->CreateRemoteFolder(m_sftpNewFolderName, &err);
            } else {
                sftp->CreateLocalFolder(m_sftpNewFolderName, &err);
            }
            m_sftpNewFolderPrompt = false;
            m_sftpNewFolderName.clear();
        }
    }

    // Yeniden Adlandırma Modal Dialogu
    if (m_sftpRenamePrompt) {
        m_r.Fill(a, 0x90000000); // karartma

        const float dw = std::floor(380 * s), dh = std::floor(140 * s);
        const float dx = std::floor((a.left + a.right) * 0.5f - dw * 0.5f);
        const float dy = std::floor((a.top + a.bottom) * 0.5f - dh * 0.5f);
        const D2D1_RECT_F dR = D2D1::RectF(dx, dy, dx + dw, dy + dh);

        m_r.FillRound(dR, 8 * s, theme::Surface);
        m_r.Stroke(dR, theme::AcHi());

        std::wstring title = Tr(Msg::SftpRenameTitle);
        m_r.Text(title, D2D1::RectF(dx + 16 * s, dy + 14 * s, dx + dw - 16 * s, dy + 34 * s), theme::TextHi, 13.5f * s, Renderer::Align::Left, true);

        m_ui.Field(ID_SFTP_RENAME, D2D1::RectF(dx + 16 * s, dy + 44 * s, dx + dw - 16 * s, dy + 76 * s), m_sftpRenameNew, Tr(Msg::SftpRenamePlaceholder));

        if (m_ui.Button(9095, D2D1::RectF(dx + dw - std::floor(170 * s), dy + std::floor(90 * s), dx + dw - std::floor(96 * s), dy + std::floor(122 * s)), Tr(Msg::ActionCancel))) {
            m_sftpRenamePrompt = false;
        }

        if (m_ui.Button(9096, D2D1::RectF(dx + dw - std::floor(86 * s), dy + std::floor(90 * s), dx + dw - 16 * s, dy + std::floor(122 * s)), Tr(Msg::ActionUpdate), !m_sftpRenameNew.empty())) {
            std::wstring err;
            if (m_sftpRenameIsRemote) {
                sftp->RenameRemoteItem(m_sftpRenameOld, m_sftpRenameNew, &err);
            } else {
                sftp->RenameLocalItem(m_sftpRenameOld, m_sftpRenameNew, &err);
            }
            m_sftpRenamePrompt = false;
            m_sftpRenameOld.clear();
            m_sftpRenameNew.clear();
        }
    }

    m_r.PopClip();
}

void MainWindow::DrawSnippetsScreen(const D2D1_RECT_F& a) {
    const float s = m_lay.scale;
    m_r.PushClip(a);

    const float pad = std::floor(22 * s);
    float x0 = a.left + pad;
    float x1 = a.right - pad;
    float y = a.top + std::floor(18 * s);

    // 1. Header (İkon + Başlık + "Yeni Snippet" Butonu)
    const D2D1_RECT_F icR = D2D1::RectF(x0, y + 2 * s, x0 + 26 * s, y + 28 * s);
    DrawIcon(Icon::Snippet, icR, theme::AcHi());
    m_r.Text(Tr(Msg::SnippetsTitle), D2D1::RectF(icR.right + 10 * s, y, x1 - 180 * s, y + 28 * s),
             theme::TextHi, 18.0f * s, Renderer::Align::Left, true);

    const float addBtnW = std::floor(150 * s), btnH = std::floor(28 * s);
    const std::wstring addSnpLabel = m_snippetAddOpen ? Tr(Msg::ActionClose) : (L"+ " + std::wstring(Tr(Msg::ActionNewSnippet)));
    if (m_ui.Button(9500, D2D1::RectF(x1 - addBtnW, y, x1, y + btnH), addSnpLabel.c_str())) {
        if (!m_snippetAddOpen) {
            m_snippetEditingId.clear();
            m_snippetNewTitle.clear();
            m_snippetNewCmd.clear();
            m_snippetNewCat = TrText(L"Custom", L"Özel");
            m_snippetNewDesc.clear();
            m_snippetNewIsYaml = false;
            m_snippetNewYaml.clear();
        }
        m_snippetAddOpen = !m_snippetAddOpen;
    }
    y += std::floor(36 * s);

    // 2. Yeni Snippet Ekle / Düzenle Formu (Açık ise)
    if (m_snippetAddOpen) {
        const float formH = m_snippetNewIsYaml ? std::floor(220 * s) : std::floor(170 * s);
        const D2D1_RECT_F formR = D2D1::RectF(x0, y, x1, y + formH);
        m_r.FillRound(formR, 6 * s, theme::Surface);
        m_r.Stroke(formR, theme::BorderHi);

        float fy = formR.top + std::floor(10 * s);
        // Tür seçimi: Standart Komut vs K8s YAML Manifesti
        const float tabW = std::floor(150 * s), tabH = std::floor(24 * s);
        const std::wstring stdCmdLabel = L"📝 " + std::wstring(Tr(Msg::SnippetsStdCmd));
        const std::wstring k8sYamlLabel = L"☸️ " + std::wstring(Tr(Msg::SnippetsK8sYaml));
        if (m_ui.Button(9490, D2D1::RectF(x0 + 12 * s, fy, x0 + 12 * s + tabW, fy + tabH), stdCmdLabel.c_str(), !m_snippetNewIsYaml)) {
            m_snippetNewIsYaml = false;
        }
        if (m_ui.Button(9491, D2D1::RectF(x0 + 16 * s + tabW, fy, x0 + 16 * s + tabW * 2, fy + tabH), k8sYamlLabel.c_str(), m_snippetNewIsYaml)) {
            m_snippetNewIsYaml = true;
            m_snippetNewCat = L"K8s";
        }
        fy += std::floor(30 * s);

        const float flw = std::floor(90 * s);

        m_r.Text(std::wstring(Tr(Msg::ColName)) + L":", D2D1::RectF(x0 + 12 * s, fy, x0 + 12 * s + flw, fy + 26 * s), theme::TextMuted, 12.0f * s);
        m_ui.Field(ID_SNP_TITLE, D2D1::RectF(x0 + 12 * s + flw, fy, x0 + std::floor(340 * s), fy + 26 * s), m_snippetNewTitle, m_snippetNewIsYaml ? L"eg. Nginx Deployment Manifest" : L"eg. Nginx Restart");

        m_r.Text(std::wstring(Tr(Msg::SnippetsCategory)) + L":", D2D1::RectF(x0 + std::floor(360 * s), fy, x0 + std::floor(430 * s), fy + 26 * s), theme::TextMuted, 12.0f * s);
        m_ui.Field(ID_SNP_CAT, D2D1::RectF(x0 + std::floor(435 * s), fy, x0 + std::floor(550 * s), fy + 26 * s), m_snippetNewCat, m_snippetNewIsYaml ? L"K8s" : L"Service");
        fy += std::floor(34 * s);

        if (!m_snippetNewIsYaml) {
            m_r.Text(std::wstring(Tr(Msg::SnippetsCommand)) + L":", D2D1::RectF(x0 + 12 * s, fy, x0 + 12 * s + flw, fy + 26 * s), theme::TextMuted, 12.0f * s);
            m_ui.Field(ID_SNP_CMD, D2D1::RectF(x0 + 12 * s + flw, fy, x1 - std::floor(14 * s), fy + 26 * s), m_snippetNewCmd, L"systemctl restart nginx");
            fy += std::floor(34 * s);
        } else {
            m_r.Text(std::wstring(Tr(Msg::SnippetsYamlCode)) + L":", D2D1::RectF(x0 + 12 * s, fy, x0 + 12 * s + flw, fy + 26 * s), theme::TextMuted, 12.0f * s);
            m_ui.Field(ID_SNP_YAML, D2D1::RectF(x0 + 12 * s + flw, fy, x1 - std::floor(14 * s), fy + std::floor(56 * s)), m_snippetNewYaml, L"apiVersion: v1\\nkind: Pod\\nmetadata:\\n  name: my-pod...");
            fy += std::floor(62 * s);
        }

        m_r.Text(std::wstring(Tr(Msg::SnippetsDescription)) + L":", D2D1::RectF(x0 + 12 * s, fy, x0 + 12 * s + flw, fy + 26 * s), theme::TextMuted, 12.0f * s);
        m_ui.Field(ID_SNP_DESC, D2D1::RectF(x0 + 12 * s + flw, fy, x1 - std::floor(140 * s), fy + 26 * s), m_snippetNewDesc, TrText(L"Optional description...", L"İsteğe bağlı açıklama..."));

        const std::wstring saveBtnText = m_snippetEditingId.empty() ? Tr(Msg::ActionSave) : Tr(Msg::ActionUpdate);
        if (m_ui.Button(9501, D2D1::RectF(x1 - std::floor(120 * s), fy, x1 - std::floor(14 * s), fy + 26 * s), saveBtnText.c_str(), true)) {
            if (!m_snippetNewTitle.empty()) {
                if (!m_snippetEditingId.empty()) {
                    m_snippets.UpdateSnippet(m_snippetEditingId, m_snippetNewTitle, m_snippetNewCmd,
                                             m_snippetNewCat, m_snippetNewDesc, m_snippetNewIsYaml, m_snippetNewYaml);
                    Toast(m_snippetNewIsYaml ? TrText(L"Kubernetes YAML updated", L"Kubernetes YAML güncellendi") : TrText(L"Snippet updated", L"Snippet güncellendi"));
                    m_snippetEditingId.clear();
                    m_snippetNewTitle.clear();
                    m_snippetNewCmd.clear();
                    m_snippetNewYaml.clear();
                    m_snippetNewDesc.clear();
                    m_snippetAddOpen = false;
                } else if (m_snippetNewIsYaml) {
                    m_snippets.AddSnippet(m_snippetNewTitle, L"", L"K8s", m_snippetNewDesc, true, m_snippetNewYaml);
                    m_snippetNewTitle.clear();
                    m_snippetNewYaml.clear();
                    m_snippetNewDesc.clear();
                    m_snippetAddOpen = false;
                    Toast(TrText(L"Kubernetes YAML snippet saved", L"Kubernetes YAML snippet kaydedildi"));
                } else if (!m_snippetNewCmd.empty()) {
                    m_snippets.AddSnippet(m_snippetNewTitle, m_snippetNewCmd, m_snippetNewCat, m_snippetNewDesc);
                    m_snippetNewTitle.clear();
                    m_snippetNewCmd.clear();
                    m_snippetNewDesc.clear();
                    m_snippetAddOpen = false;
                    Toast(TrText(L"Snippet saved", L"Snippet kaydedildi"));
                }
            }
        }
        y = formR.bottom + std::floor(16 * s);
    }

    // 3. Arama Kutusu ve Kategori Butonları
    const float searchW = std::floor(220 * s);
    m_ui.Field(ID_SNP_SEARCH, D2D1::RectF(x0, y, x0 + searchW, y + std::floor(28 * s)), m_snippetSearch, Tr(Msg::SnippetsSearch));

    auto GetCatDisplay = [](const std::wstring& cat) -> std::wstring {
        if (cat == L"All" || cat == L"Tümü") return TrText(L"All", L"Tümü");
        if (cat == L"System" || cat == L"Sistem") return TrText(L"System", L"Sistem");
        if (cat == L"Network" || cat == L"Ağ") return TrText(L"Network", L"Ağ");
        if (cat == L"Service" || cat == L"Servis") return TrText(L"Service", L"Servis");
        if (cat == L"Security" || cat == L"Güvenlik") return TrText(L"Security", L"Güvenlik");
        if (cat == L"Custom" || cat == L"Özel") return TrText(L"Custom", L"Özel");
        return cat;
    };

    float pillX = x0 + searchW + std::floor(16 * s);
    const auto cats = m_snippets.GetCategories();
    for (size_t i = 0; i < cats.size(); ++i) {
        const std::wstring dispCat = GetCatDisplay(cats[i]);
        const float pillW = std::floor(m_r.MeasureText(dispCat, 11.5f * s) + 20 * s);
        if (pillX + pillW > x1) break;
        const D2D1_RECT_F pR = D2D1::RectF(pillX, y, pillX + pillW, y + std::floor(28 * s));
        const bool active = (m_snippetCategory == cats[i]);
        if (m_ui.Button(9510 + (int)i, pR, dispCat.c_str(), active)) {
            m_snippetCategory = cats[i];
            m_snippetScroll = 0.0f;
        }
        pillX += pillW + std::floor(6 * s);
    }
    y += std::floor(38 * s);

    // 4. Snippet Kartları Listesi
    const auto items = m_snippets.GetFiltered(m_snippetCategory, m_snippetSearch);
    const float listTop = y;
    const float listBottom = a.bottom - std::floor(14 * s);
    const float viewH = std::max(1.0f, listBottom - listTop);
    const float cardH = std::floor(86 * s);
    const float totalH = items.size() * (cardH + std::floor(10 * s));
    const float maxScroll = std::max(0.0f, totalH - viewH);
    m_snippetScroll = std::clamp(m_snippetScroll, 0.0f, maxScroll);

    const D2D1_RECT_F clipR = D2D1::RectF(x0, listTop, x1, listBottom);
    m_r.PushClip(clipR);

    float cy = listTop - m_snippetScroll;
    for (size_t i = 0; i < items.size(); ++i) {
        if (cy + cardH >= listTop && cy <= listBottom) {
            const auto& snp = items[i];
            const D2D1_RECT_F cR = D2D1::RectF(x0, cy, x1 - (maxScroll > 0 ? std::floor(12 * s) : 0), cy + cardH);
            if (m_ui.Hot(cR)) m_r.FillRound(cR, 6 * s, theme::Surface);
            else m_r.FillRound(cR, 6 * s, theme::TermBg);
            m_r.Stroke(cR, theme::Border);

            // Başlık & Kategori rozeti
            const float titleRight = snp.isYaml ? (cR.right - std::floor(270 * s)) : (cR.right - std::floor(260 * s));
            m_r.Text(snp.title, D2D1::RectF(cR.left + 14 * s, cR.top + 8 * s, titleRight, cR.top + 26 * s),
                     theme::TextHi, 13.0f * s, Renderer::Align::Left, true);

            const D2D1_RECT_F bR = D2D1::RectF(cR.right - 250 * s, cR.top + 8 * s, cR.right - 180 * s, cR.top + 26 * s);
            m_ui.Badge(bR, snp.isYaml ? L"☸️ K8s YAML" : GetCatDisplay(snp.category), snp.isYaml ? 0x326CE5 : theme::AcHi());

            // Komut / YAML Kutusu
            const float cmdR = snp.isYaml ? (cR.right - std::floor(256 * s)) : (cR.right - std::floor(180 * s));
            const D2D1_RECT_F cmdBox = D2D1::RectF(cR.left + 14 * s, cR.top + 30 * s, cmdR, cR.top + 56 * s);
            m_r.FillRound(cmdBox, 4 * s, theme::TermBg);
            m_r.Stroke(cmdBox, theme::Border);
            std::wstring dispCmd = snp.isYaml ? (snp.yamlContent.empty() ? snp.command : L"☸️ " + snp.yamlContent) : snp.command;
            m_r.Text(dispCmd, D2D1::RectF(cmdBox.left + 8 * s, cmdBox.top, cmdBox.right - 8 * s, cmdBox.bottom),
                     snp.isYaml ? 0x326CE5 : theme::Green, 11.5f * s, Renderer::Align::Left, false, true);

            // Açıklama
            if (!snp.description.empty()) {
                m_r.Text(snp.description, D2D1::RectF(cR.left + 14 * s, cR.top + 58 * s, cmdR, cR.bottom - 4 * s),
                         theme::TextDim, 10.5f * s);
            }

            // Butonlar
            const float abtnH = std::floor(26 * s);
            const float btnY = cR.top + (cardH - abtnH) * 0.5f;

            if (snp.isYaml) {
                const float editW = std::floor(30 * s);
                const float expW = std::floor(70 * s);
                const float applyW = std::floor(80 * s);
                const float copyW = std::floor(64 * s);
                const float rightPad = snp.isCustom ? std::floor(36 * s) : std::floor(10 * s);

                const D2D1_RECT_F edR = D2D1::RectF(cR.right - rightPad - editW, btnY, cR.right - rightPad, btnY + abtnH);
                const D2D1_RECT_F cpR = D2D1::RectF(edR.left - std::floor(6 * s) - copyW, btnY, edR.left - std::floor(6 * s), btnY + abtnH);
                const D2D1_RECT_F expR = D2D1::RectF(cpR.left - std::floor(6 * s) - expW, btnY, cpR.left - std::floor(6 * s), btnY + abtnH);
                const D2D1_RECT_F apR = D2D1::RectF(expR.left - std::floor(6 * s) - applyW, btnY, expR.left - std::floor(6 * s), btnY + abtnH);

                const std::wstring applyLabel = L"🚀 " + std::wstring(TrText(L"Apply", L"Uygula"));
                if (m_ui.Button(9600 + (int)i, apR, applyLabel.c_str(), true)) {
                    if (auto* t = Active()) {
                        std::string toSend = "cat << 'EOF' | kubectl apply -f -\n" + WideToUtf8(snp.yamlContent) + "\nEOF\n";
                        if (m_broadcastMode) BroadcastInput(toSend);
                        else t->Write(toSend);
                        SetView(View::Terminal);
                        Toast(TrText(L"Kubernetes YAML sent to terminal", L"Kubernetes YAML terminale gönderildi"));
                    } else {
                        Toast(TrText(L"No active terminal tab", L"Aktif terminal sekmesi yok"));
                    }
                }

                if (m_ui.Button(9650 + (int)i, expR, L"📥 Export")) {
                    std::wstring outPath;
                    if (m_snippets.ExportYaml(snp, m_dataDir + L"\\k8s", outPath)) {
                        Toast(TrText(L"YAML exported: ", L"YAML dışa aktarıldı: ") + outPath);
                    } else {
                        Toast(TrText(L"YAML export failed", L"YAML aktarımı başarısız"));
                    }
                }

                const std::wstring copySnp = L"📋 " + std::wstring(Tr(Msg::MenuCopy));
                if (m_ui.Button(9700 + (int)i, cpR, copySnp.c_str())) {
                    ClipboardSetText(m_hwnd, snp.yamlContent.empty() ? snp.command : snp.yamlContent);
                    Toast(TrText(L"YAML content copied to clipboard", L"YAML içeriği panoya kopyalandı"));
                }

                if (m_ui.Button(9750 + (int)i, edR, L"✏️")) {
                    m_snippetEditingId = snp.id;
                    m_snippetNewTitle = snp.title;
                    m_snippetNewCmd = snp.command;
                    m_snippetNewCat = snp.category;
                    m_snippetNewDesc = snp.description;
                    m_snippetNewIsYaml = snp.isYaml;
                    m_snippetNewYaml = snp.yamlContent;
                    m_snippetAddOpen = true;
                }
            } else {
                const float editW = std::floor(30 * s);
                const float copyW = std::floor(64 * s);
                const float actionW = std::floor(76 * s);
                const float rightPad = snp.isCustom ? std::floor(36 * s) : std::floor(10 * s);

                const D2D1_RECT_F edR = D2D1::RectF(cR.right - rightPad - editW, btnY, cR.right - rightPad, btnY + abtnH);
                const D2D1_RECT_F cpR = D2D1::RectF(edR.left - std::floor(6 * s) - copyW, btnY, edR.left - std::floor(6 * s), btnY + abtnH);
                const D2D1_RECT_F runR = D2D1::RectF(cpR.left - std::floor(6 * s) - actionW, btnY, cpR.left - std::floor(6 * s), btnY + abtnH);

                const std::wstring runLabel = L"▶ " + std::wstring(Tr(Msg::ActionStart));
                if (m_ui.Button(9600 + (int)i, runR, runLabel.c_str(), true)) {
                    if (auto* t = Active()) {
                        std::string u8cmd = WideToUtf8(snp.command) + "\n";
                        if (m_broadcastMode) BroadcastInput(u8cmd);
                        else t->Write(u8cmd.data(), u8cmd.size());
                        SetView(View::Terminal);
                        Toast(TrText(L"Command sent to terminal", L"Komut terminale gönderildi"));
                    } else {
                        Toast(TrText(L"No active terminal tab", L"Aktif terminal sekmesi yok"));
                    }
                }

                const std::wstring copyCmd = L"📋 " + std::wstring(Tr(Msg::MenuCopy));
                if (m_ui.Button(9700 + (int)i, cpR, copyCmd.c_str())) {
                    ClipboardSetText(m_hwnd, snp.command);
                    Toast(TrText(L"Command copied to clipboard", L"Komut panoya kopyalandı"));
                }

                if (m_ui.Button(9750 + (int)i, edR, L"✏️")) {
                    m_snippetEditingId = snp.id;
                    m_snippetNewTitle = snp.title;
                    m_snippetNewCmd = snp.command;
                    m_snippetNewCat = snp.category;
                    m_snippetNewDesc = snp.description;
                    m_snippetNewIsYaml = snp.isYaml;
                    m_snippetNewYaml = snp.yamlContent;
                    m_snippetAddOpen = true;
                }
            }

            if (snp.isCustom) {
                if (m_ui.Button(9800 + (int)i, D2D1::RectF(cR.right - 10 * s - std::floor(24 * s), cR.top + 6 * s, cR.right - 10 * s, cR.top + std::floor(22 * s)), L"×", false, true)) {
                    m_snippets.DeleteSnippet(snp.id);
                    Toast(TrText(L"Snippet deleted", L"Snippet silindi"));
                    break;
                }
            }
        }
        cy += cardH + std::floor(10 * s);
    }


    if (items.empty()) {
        m_r.Text(TrText(L"No snippets match this criteria.", L"Bu kritere uygun snippet bulunamadı."), D2D1::RectF(x0, listTop + std::floor(30 * s), x1, listTop + std::floor(60 * s)),
                 theme::TextMuted, 13.0f * s, Renderer::Align::Center);
    }

    m_r.PopClip();

    // Scrollbar
    if (maxScroll > 0.0f) {
        const float sbW = std::floor(5 * s);
        const float sbX = x1 - sbW;
        const float thumbH = std::max(std::floor(24 * s), (viewH / totalH) * viewH);
        const float thumbY = listTop + (m_snippetScroll / maxScroll) * (viewH - thumbH);
        m_r.FillRound(D2D1::RectF(sbX, thumbY, sbX + sbW, thumbY + thumbH), sbW * 0.5f, theme::BorderHi);
    }

    m_r.PopClip();
}

void MainWindow::DrawPortForwardScreen(const D2D1_RECT_F& a) {
    const float s = m_lay.scale;
    m_r.PushClip(a);

    const float pad = std::floor(22 * s);
    float x0 = a.left + pad;
    float x1 = a.right - pad;
    float y = a.top + std::floor(18 * s);

    // 1. Header (İkon + Başlık + "Yeni Tünel" Butonu)
    const D2D1_RECT_F icR = D2D1::RectF(x0, y + 2 * s, x0 + 26 * s, y + 28 * s);
    DrawIcon(Icon::Forward, icR, theme::AcHi());
    m_r.Text(Tr(Msg::TunnelTitle), D2D1::RectF(icR.right + 10 * s, y, x1 - 180 * s, y + 28 * s),
             theme::TextHi, 18.0f * s, Renderer::Align::Left, true);

    const float addBtnW = std::floor(140 * s), btnH = std::floor(28 * s);
    const std::wstring addBtnText = m_tunnelAddOpen ? Tr(Msg::ActionCancel) : (L"+ " + std::wstring(Tr(Msg::ActionNewTunnel)));
    if (m_ui.Button(9900, D2D1::RectF(x1 - addBtnW, y, x1, y + btnH), addBtnText.c_str())) {
        m_tunnelAddOpen = !m_tunnelAddOpen;
    }
    y += std::floor(36 * s);

    // 2. Yeni Tünel Formu
    if (m_tunnelAddOpen) {
        const D2D1_RECT_F formR = D2D1::RectF(x0, y, x1, y + std::floor(160 * s));
        m_r.FillRound(formR, 6 * s, theme::Surface);
        m_r.Stroke(formR, theme::BorderHi);

        float fy = formR.top + std::floor(12 * s);
        const float flw = std::floor(90 * s);

        m_r.Text(TrText(L"Tunnel Name:", L"Tünel Adı:"), D2D1::RectF(x0 + 12 * s, fy, x0 + 12 * s + flw, fy + 26 * s), theme::TextMuted, 12.0f * s);
        m_ui.Field(ID_TUN_NAME, D2D1::RectF(x0 + 12 * s + flw, fy, x0 + std::floor(280 * s), fy + 26 * s), m_tunnelNewName, TrText(L"e.g. Postgres DB", L"Örn: Postgres DB"));

        m_r.Text(TrText(L"Type:", L"Tür:"), D2D1::RectF(x0 + std::floor(300 * s), fy, x0 + std::floor(340 * s), fy + 26 * s), theme::TextMuted, 12.0f * s);
        const std::vector<std::wstring> typeOpts = { Tr(Msg::TunnelTypeLocal), Tr(Msg::TunnelTypeRemote), Tr(Msg::TunnelTypeDynamic) };
        m_ui.Choice(9901, D2D1::RectF(x0 + std::floor(345 * s), fy, x0 + std::floor(470 * s), fy + 26 * s), typeOpts, m_tunnelNewType);

        m_r.Text(TrText(L"Host ID/Name:", L"Host ID/Adı:"), D2D1::RectF(x0 + std::floor(485 * s), fy, x0 + std::floor(565 * s), fy + 26 * s), theme::TextMuted, 12.0f * s);
        m_ui.Field(ID_TUN_HOST, D2D1::RectF(x0 + std::floor(570 * s), fy, x1 - std::floor(14 * s), fy + 26 * s), m_tunnelNewHostId, TrText(L"Registered host name or address", L"Kayıtlı host adı veya adresi"));
        fy += std::floor(34 * s);

        m_r.Text(TrText(L"Local Port:", L"Yerel Port:"), D2D1::RectF(x0 + 12 * s, fy, x0 + 12 * s + flw, fy + 26 * s), theme::TextMuted, 12.0f * s);
        m_ui.Field(ID_TUN_LPORT, D2D1::RectF(x0 + 12 * s + flw, fy, x0 + std::floor(200 * s), fy + 26 * s), m_tunnelNewLocalPort, L"8080");

        if (m_tunnelNewType != 2) { // Dinamik haric
            m_r.Text(TrText(L"Destination:", L"Hedef Adres:"), D2D1::RectF(x0 + std::floor(220 * s), fy, x0 + std::floor(310 * s), fy + 26 * s), theme::TextMuted, 12.0f * s);
            m_ui.Field(ID_TUN_RHOST, D2D1::RectF(x0 + std::floor(315 * s), fy, x0 + std::floor(470 * s), fy + 26 * s), m_tunnelNewRemoteHost, L"localhost");

            m_r.Text(TrText(L"Dest Port:", L"Hedef Port:"), D2D1::RectF(x0 + std::floor(485 * s), fy, x0 + std::floor(565 * s), fy + 26 * s), theme::TextMuted, 12.0f * s);
            m_ui.Field(ID_TUN_RPORT, D2D1::RectF(x0 + std::floor(570 * s), fy, x0 + std::floor(650 * s), fy + 26 * s), m_tunnelNewRemotePort, L"80");
        }

        if (m_ui.Button(9902, D2D1::RectF(x1 - std::floor(110 * s), fy, x1 - std::floor(14 * s), fy + 26 * s), Tr(Msg::ActionSave), true)) {
            if (!m_tunnelNewName.empty() && !m_tunnelNewHostId.empty()) {
                m_tunnels.AddRule(m_tunnelNewName, m_tunnelNewHostId, (TunnelType)m_tunnelNewType,
                                 _wtoi(m_tunnelNewLocalPort.c_str()), m_tunnelNewRemoteHost, _wtoi(m_tunnelNewRemotePort.c_str()));
                m_tunnelNewName.clear();
                m_tunnelAddOpen = false;
                Toast(TrText(L"Tunnel rule added", L"Tünel kuralı eklendi"));
            }
        }
        y = formR.bottom + std::floor(16 * s);
    }

    // Periyodik durum kontrolü
    m_tunnels.CheckStatus();

    // 3. Tünel Kartları Listesi
    const auto& rules = m_tunnels.Rules();
    const float listTop = y;
    const float listBottom = a.bottom - std::floor(14 * s);
    const float viewH = std::max(1.0f, listBottom - listTop);
    const float cardH = std::floor(76 * s);
    const float totalH = rules.size() * (cardH + std::floor(10 * s));
    const float maxScroll = std::max(0.0f, totalH - viewH);
    m_tunnelScroll = std::clamp(m_tunnelScroll, 0.0f, maxScroll);

    const D2D1_RECT_F clipR = D2D1::RectF(x0, listTop, x1, listBottom);
    m_r.PushClip(clipR);

    float cy = listTop - m_tunnelScroll;
    for (size_t i = 0; i < rules.size(); ++i) {
        if (cy + cardH >= listTop && cy <= listBottom) {
            const auto& r = rules[i];
            const D2D1_RECT_F cR = D2D1::RectF(x0, cy, x1 - (maxScroll > 0 ? std::floor(12 * s) : 0), cy + cardH);
            if (m_ui.Hot(cR)) m_r.FillRound(cR, 6 * s, theme::Surface);
            else m_r.FillRound(cR, 6 * s, theme::TermBg);
            m_r.Stroke(cR, r.running ? theme::Green : theme::Border);

            // Başlık & Host
            m_r.Text(r.name, D2D1::RectF(cR.left + 14 * s, cR.top + 8 * s, cR.left + std::floor(200 * s), cR.top + 26 * s),
                     theme::TextHi, 13.5f * s, Renderer::Align::Left, true);

            // Tür Rozeti
            std::wstring typeStr = (r.type == TunnelType::Local) ? Tr(Msg::TunnelTypeLocal) :
                                   (r.type == TunnelType::Remote) ? Tr(Msg::TunnelTypeRemote) : L"SOCKS5 (-D)";
            const D2D1_RECT_F typeBadge = D2D1::RectF(cR.left + std::floor(210 * s), cR.top + 8 * s, cR.left + std::floor(300 * s), cR.top + 26 * s);
            m_ui.Badge(typeBadge, typeStr, theme::AcHi());

            // Durum Rozeti
            const D2D1_RECT_F stBadge = D2D1::RectF(cR.left + std::floor(310 * s), cR.top + 8 * s, cR.left + std::floor(430 * s), cR.top + 26 * s);
            if (r.running) {
                m_ui.Badge(stBadge, L"● " + std::wstring(Tr(Msg::TunnelActiveBadge)) + L" (PID " + std::to_wstring(r.pid) + L")", theme::Green);
            } else {
                m_ui.Badge(stBadge, L"○ " + std::wstring(Tr(Msg::TunnelStoppedBadge)), theme::TextMuted);
            }

            // Port Haritası
            std::wstring mapStr;
            if (r.type == TunnelType::Local) {
                mapStr = L"127.0.0.1:" + std::to_wstring(r.localPort) + L"  ──▶  " + r.remoteHost + L":" + std::to_wstring(r.remotePort) + L" (via " + r.hostDisplay + L")";
            } else if (r.type == TunnelType::Remote) {
                mapStr = r.remoteHost + L":" + std::to_wstring(r.remotePort) + L"  ──▶  127.0.0.1:" + std::to_wstring(r.localPort) + L" (via " + r.hostDisplay + L")";
            } else {
                mapStr = L"SOCKS5 Proxy: 127.0.0.1:" + std::to_wstring(r.localPort) + L" (via " + r.hostDisplay + L")";
            }
            m_r.Text(mapStr, D2D1::RectF(cR.left + 14 * s, cR.top + 34 * s, cR.right - 200 * s, cR.bottom - 6 * s),
                     theme::Ac(), 12.0f * s, Renderer::Align::Left, false, true);

            // Butonlar: Başlat / Durdur, Sil
            const float btnW = std::floor(80 * s), abtnH = std::floor(26 * s);
            const float btnY = cR.top + (cardH - abtnH) * 0.5f;

            if (r.running) {
                if (m_ui.Button(9950 + (int)i, D2D1::RectF(cR.right - 180 * s, btnY, cR.right - 180 * s + btnW, btnY + abtnH), Tr(Msg::ActionStop), false, true)) {
                    m_tunnels.StopTunnel(r.id);
                    Toast(TrText(L"Tunnel stopped", L"Tünel durduruldu"));
                }
            } else {
                if (m_ui.Button(9950 + (int)i, D2D1::RectF(cR.right - 180 * s, btnY, cR.right - 180 * s + btnW, btnY + abtnH), Tr(Msg::ActionStart), true)) {
                    std::wstring err;
                    if (m_tunnels.StartTunnel(r.id, &err)) {
                        Toast(TrText(L"Tunnel started", L"Tünel başlatıldı"));
                    } else {
                        Toast(std::wstring(TrText(L"Tunnel error: ", L"Tünel hatası: ")) + err);
                    }
                }
            }

            if (m_ui.Button(9980 + (int)i, D2D1::RectF(cR.right - 90 * s, btnY, cR.right - 10 * s, btnY + abtnH), Tr(Msg::ActionDelete), false, true)) {
                m_tunnels.DeleteRule(r.id);
                Toast(TrText(L"Tunnel deleted", L"Tünel silindi"));
                break;
            }
        }
        cy += cardH + std::floor(10 * s);
    }

    if (rules.empty()) {
        m_r.Text(Tr(Msg::TunnelEmpty),
                 D2D1::RectF(x0, listTop + std::floor(30 * s), x1, listTop + std::floor(60 * s)),
                 theme::TextMuted, 13.0f * s, Renderer::Align::Center);
    }

    m_r.PopClip();

    // Scrollbar
    if (maxScroll > 0.0f) {
        const float sbW = std::floor(5 * s);
        const float sbX = x1 - sbW;
        const float thumbH = std::max(std::floor(24 * s), (viewH / totalH) * viewH);
        const float thumbY = listTop + (m_tunnelScroll / maxScroll) * (viewH - thumbH);
        m_r.FillRound(D2D1::RectF(sbX, thumbY, sbX + sbW, thumbY + thumbH), sbW * 0.5f, theme::BorderHi);
    }

    m_r.PopClip();
}

void MainWindow::DrawLogsScreen(const D2D1_RECT_F& a) {
    const float s = m_lay.scale;
    m_r.PushClip(a);

    const float pad = std::floor(22 * s);
    float x0 = a.left + pad;
    float x1 = a.right - pad;
    float y = a.top + std::floor(18 * s);

    // 1. Header (İkon + Başlık)
    const D2D1_RECT_F icR = D2D1::RectF(x0, y + 2 * s, x0 + 26 * s, y + 28 * s);
    DrawIcon(Icon::Clock, icR, theme::AcHi());
    m_r.Text(Tr(Msg::LogsTitle), D2D1::RectF(icR.right + 10 * s, y, x1 - 200 * s, y + 28 * s),
             theme::TextHi, 18.0f * s, Renderer::Align::Left, true);

    // Oturum Kaydı Başlat / Durdur
    auto* activeTab = Active();
    const bool isRec = activeTab && activeTab->Recorder() && activeTab->Recorder()->IsRecording();
    const float recBtnW = std::floor(170 * s), btnH = std::floor(28 * s);
    if (m_ui.Button(9250, D2D1::RectF(x1 - recBtnW, y, x1, y + btnH),
                    isRec ? Tr(Msg::LogsStopRecording) : Tr(Msg::LogsRecordSession), isRec)) {
        ToggleSessionRecording();
    }
    y += std::floor(36 * s);

    // 2. Alt Sekmeler: [Canlı Terminal Logları] [Kayıtlı Oturumlar (.cast)]
    const std::vector<std::wstring> tabs = { Tr(Msg::LogsTabLive), Tr(Msg::LogsTabRecordings) };
    float tabX = x0;
    for (int i = 0; i < (int)tabs.size(); ++i) {
        const float tw = std::floor(m_r.MeasureText(tabs[i], 12.0f * s) + 24 * s);
        const D2D1_RECT_F tr = D2D1::RectF(tabX, y, tabX + tw, y + std::floor(30 * s));
        const bool active = (m_logsTab == i);
        if (m_ui.Button(9260 + i, tr, tabs[i].c_str(), active)) {
            m_logsTab = i;
            m_logsScroll = 0.0f;
        }
        tabX += tw + std::floor(8 * s);
    }
    y += std::floor(40 * s);

    const float listTop = y;
    const float listBottom = a.bottom - std::floor(14 * s);
    const float viewH = std::max(1.0f, listBottom - listTop);

    if (m_logsTab == 0) {
        // Canlı Terminal Logları
        if (activeTab) {
            const auto& logs = activeTab->GetSshLogs();
            const float lineH = std::floor(24 * s);
            const float totalH = logs.size() * lineH;
            const float maxScroll = std::max(0.0f, totalH - viewH);
            m_logsScroll = std::clamp(m_logsScroll, 0.0f, maxScroll);

            const D2D1_RECT_F clipR = D2D1::RectF(x0, listTop, x1, listBottom);
            m_r.PushClip(clipR);

            float ly = listTop - m_logsScroll;
            for (size_t i = 0; i < logs.size(); ++i) {
                if (ly + lineH >= listTop && ly <= listBottom) {
                    const auto& entry = logs[i];
                    m_r.Text(entry.icon, D2D1::RectF(x0 + 8 * s, ly, x0 + 28 * s, ly + lineH), theme::TextDim, 11.5f * s);
                    m_r.Text(entry.text, D2D1::RectF(x0 + 32 * s, ly, x1 - 10 * s, ly + lineH), entry.color ? entry.color : theme::Text, 11.5f * s,
                             Renderer::Align::Left, false, true);
                    m_r.Line(x0, ly + lineH, x1, ly + lineH, theme::Border, 0.5f);
                }
                ly += lineH;
            }

            if (logs.empty()) {
                m_r.Text(Tr(Msg::LogsEmptyLive), D2D1::RectF(x0, listTop + std::floor(30 * s), x1, listTop + std::floor(60 * s)),
                         theme::TextMuted, 13.0f * s, Renderer::Align::Center);
            }

            m_r.PopClip();

            // Scrollbar
            if (maxScroll > 0.0f) {
                const float sbW = std::floor(5 * s);
                const float sbX = x1 - sbW;
                const float thumbH = std::max(std::floor(24 * s), (viewH / totalH) * viewH);
                const float thumbY = listTop + (m_logsScroll / maxScroll) * (viewH - thumbH);
                m_r.FillRound(D2D1::RectF(sbX, thumbY, sbX + sbW, thumbY + thumbH), sbW * 0.5f, theme::BorderHi);
            }
        } else {
            m_r.Text(TrText(L"No active terminal session.", L"Aktif terminal oturumu bulunmuyor."), D2D1::RectF(x0, listTop + std::floor(30 * s), x1, listTop + std::floor(60 * s)),
                     theme::TextMuted, 13.0f * s, Renderer::Align::Center);
        }
    }
    else if (m_logsTab == 1) {
        // Kaydedilmiş Oturumlar (.cast / .ftrec)
        std::vector<FileItem> recFiles;
        std::wstring recDir = m_dataDir + L"\\recordings";
        CreateDirectoryW(recDir.c_str(), nullptr);

        std::wstring err;
        LocalFileSystem lfs;
        recFiles = lfs.List(recDir, &err);

        // ".." elemanini filtrele
        std::vector<FileItem> actualRecs;
        for (const auto& item : recFiles) {
            if (!item.isDir && (item.name.ends_with(L".cast") || item.name.ends_with(L".ftrec"))) {
                actualRecs.push_back(item);
            }
        }

        const float rowH = std::floor(44 * s);
        const float totalH = actualRecs.size() * (rowH + std::floor(6 * s));
        const float maxScroll = std::max(0.0f, totalH - viewH);
        m_logsScroll = std::clamp(m_logsScroll, 0.0f, maxScroll);

        const D2D1_RECT_F clipR = D2D1::RectF(x0, listTop, x1, listBottom);
        m_r.PushClip(clipR);

        float ry = listTop - m_logsScroll;
        for (size_t i = 0; i < actualRecs.size(); ++i) {
            if (ry + rowH >= listTop && ry <= listBottom) {
                const auto& item = actualRecs[i];
                const D2D1_RECT_F r = D2D1::RectF(x0, ry, x1 - (maxScroll > 0 ? std::floor(12 * s) : 0), ry + rowH);
                if (m_ui.Hot(r)) m_r.FillRound(r, 6 * s, theme::Surface);
                else m_r.FillRound(r, 6 * s, theme::TermBg);
                m_r.Stroke(r, theme::Border);

                const D2D1_RECT_F ic = D2D1::RectF(r.left + 10 * s, r.top + 10 * s, r.left + 30 * s, r.bottom - 10 * s);
                DrawIcon(Icon::Clock, ic, theme::AcHi());

                m_r.Text(item.name, D2D1::RectF(r.left + 38 * s, r.top + 6 * s, r.right - 260 * s, r.top + 24 * s),
                         theme::TextHi, 12.5f * s, Renderer::Align::Left, true);

                std::wstring sub = item.modified + L"  •  " + LocalFileSystem::FormatFileSize(item.size);
                m_r.Text(sub, D2D1::RectF(r.left + 38 * s, r.top + 24 * s, r.right - 260 * s, r.bottom - 4 * s),
                         theme::TextDim, 10.5f * s);

                // Klasörde Aç Butonu
                const float abtnW = std::floor(76 * s), abtnH = std::floor(26 * s);
                const float btnY = r.top + (rowH - abtnH) * 0.5f;

                if (m_ui.Button(9330 + (int)i, D2D1::RectF(r.right - 96 * s, btnY, r.right - 10 * s, btnY + abtnH), Tr(Msg::LogsOpenFolder))) {
                    ShellExecuteW(nullptr, L"open", recDir.c_str(), nullptr, nullptr, SW_SHOW);
                }
            }
            ry += rowH + std::floor(6 * s);
        }

        if (actualRecs.empty()) {
            m_r.Text(Tr(Msg::LogsEmptyRecordings),
                     D2D1::RectF(x0, listTop + std::floor(30 * s), x1, listTop + std::floor(60 * s)),
                     theme::TextMuted, 13.0f * s, Renderer::Align::Center);
        }

        m_r.PopClip();

        // Scrollbar
        if (maxScroll > 0.0f) {
            const float sbW = std::floor(5 * s);
            const float sbX = x1 - sbW;
            const float thumbH = std::max(std::floor(24 * s), (viewH / totalH) * viewH);
            const float thumbY = listTop + (m_logsScroll / maxScroll) * (viewH - thumbH);
            m_r.FillRound(D2D1::RectF(sbX, thumbY, sbX + sbW, thumbY + thumbH), sbW * 0.5f, theme::BorderHi);
        }
    }

    m_r.PopClip();
}

void MainWindow::DrawHostsScreen(const D2D1_RECT_F& a) {
    const float s = m_lay.scale;
    m_r.PushClip(a);

    if (m_hostDetail) {
        if (Host* h = m_inv.FindHost(m_selHost)) { DrawHostDetail(a, *h); m_r.PopClip(); return; }
        m_hostDetail = false;
        m_mainScroll = 0.0f;
        RefreshHubNodes(false);
    }

    // Her karede degil: tarama bos donerse surekli yeni surec baslatirdi.
    if (m_hubNodes.empty() && m_lastHubScan == 0) {
        RefreshHubNodes();
    }

    const float pad = std::floor(22 * s);
    const float x0 = a.left + pad;
    const float x1 = a.right - pad;
    float y = a.top + std::floor(16 * s);

    // --- 1. XPipe Altyapi Hub & MCP Baslik Alani ---
    {
        m_r.Text(Tr(Msg::HubTitle),
                 D2D1::RectF(x0, y, x1 - std::floor(220 * s), y + std::floor(24 * s)),
                 theme::TextHi, 15.5f * s, Renderer::Align::Left, true);

        // Sag ustte MCP Sunucu Rozeti & Butonu
        const float badgeW = std::floor(200 * s);
        const float badgeH = std::floor(26 * s);
        const D2D1_RECT_F mcpBadge = D2D1::RectF(x1 - badgeW, y, x1, y + badgeH);
        // Rozet gercek durumu soylesin: sunucu ayarlardan kapaliysa "Aktif" yazmak
        // kullaniciyi yaniltir (istemci baglanir ama hicbir arac calismaz).
        const bool mcpOn = m_cfg.mcpEnabled && m_cfg.mcpStdio;
        const std::wstring mcpLabel = mcpOn ? TrText(L"AI / MCP: stdio active", L"AI / MCP: stdio açık")
                                            : TrText(L"AI / MCP: disabled", L"AI / MCP: kapalı");
        if (m_ui.Button(6005, mcpBadge, mcpLabel.c_str(), false)) {
            wchar_t exePath[MAX_PATH]{};
            GetModuleFileNameW(nullptr, exePath, MAX_PATH);
            ClipboardSetText(m_hwnd, L"\"" + std::wstring(exePath) + L"\" --mcp");
            Toast(mcpOn ? TrText(L"MCP command copied to clipboard: FullTerminal.exe --mcp", L"MCP komutu panoya kopyalandı: FullTerminal.exe --mcp")
                        : TrText(L"MCP command copied. Server disabled: Enable from Settings > MCP.", L"MCP komutu kopyalandı. Sunucu kapalı: Ayarlar > MCP'den etkinleştir."));
        }

        y += std::floor(26 * s);
        m_r.Text(Tr(Msg::HubSubtitle),
                 D2D1::RectF(x0, y, x1, y + std::floor(18 * s)),
                 theme::TextMuted, 11.5f * s, Renderer::Align::Left, false, true);
        y += std::floor(22 * s);
    }

    // --- 2. Hizli baglanti & Arama cubugu ---
    {
        const float h = std::floor(36 * s);
        const float btnW = std::floor(86 * s);
        const float refreshW = std::floor(80 * s);
        const float newHostW = std::floor(100 * s);
        const float termW = std::floor(84 * s);

        const float fieldW = std::max(120.0f, x1 - x0 - (btnW + refreshW + newHostW + termW + std::floor(32 * s)));
        const D2D1_RECT_F fr = D2D1::RectF(x0, y, x0 + fieldW, y + h);
        const D2D1_RECT_F ir = D2D1::RectF(fr.left + std::floor(6 * s), fr.top, fr.left + std::floor(32 * s), fr.bottom);

        m_ui.Field(ID_QUICK, fr, m_quick, Tr(Msg::HubSearchPlaceholder),
                   false, std::floor(26 * s));
        DrawIcon(Icon::Search, ir, theme::TextDim);

        float bx = fr.right + std::floor(8 * s);
        const bool canConn = !TrimWs(m_quick).empty();
        if (m_ui.Button(6000, D2D1::RectF(bx, y, bx + btnW, y + h), Tr(Msg::ActionConnect), true, false, canConn) && canConn) {
            QuickConnect(m_quick);
        }
        bx += btnW + std::floor(8 * s);

        if (m_ui.Button(6001, D2D1::RectF(bx, y, bx + refreshW, y + h), Tr(Msg::ActionRefresh))) {
            // K8s klasorune elle eklenen/silinen manifestler de gorunsun.
            K8sManager::Instance().Rescan(m_dataDir);
            RefreshHubNodes();
            Toast(TrText(L"Rescanning systems...", L"Sistemler yeniden taranıyor..."));
        }
        bx += refreshW + std::floor(8 * s);

        const std::wstring newHostLabel = L"+ " + std::wstring(Tr(Msg::ActionNewHost));
        if (m_ui.Button(6010, D2D1::RectF(bx, y, bx + newHostW, y + h), newHostLabel.c_str())) {
            Host& h2 = m_inv.AddHost();
            m_selHost = h2.id;
            m_hostDetail = true;
            m_hostDirty = true;
            m_mainScroll = 0.0f;
            RefreshHubNodes(false);
        }
        bx += newHostW + std::floor(8 * s);

        if (m_ui.Button(6011, D2D1::RectF(bx, y, bx + termW, y + h), Tr(Msg::TabTerminal))) {
            POINT p{ (LONG)bx, (LONG)(y + h) };
            ClientToScreen(m_hwnd, &p);
            ShowProfileMenu(p);
        }

        y += h + std::floor(12 * s);
    }

    // --- 3. Kategori Filtreleri (Choice Segmenti) ---
    size_t nDocker = 0, nWsl = 0, nLocal = 0, nSsh = 0, nK8s = 0;
    for (const auto& nd : m_hubNodes) {
        if (nd.type == NodeType::DockerContainer) ++nDocker;
        else if (nd.type == NodeType::Wsl) ++nWsl;
        else if (nd.type == NodeType::Local) ++nLocal;
        else if (nd.type == NodeType::SshHost) ++nSsh;
        else if (nd.type == NodeType::K8sPod) ++nK8s;
    }

    std::vector<std::wstring> catLabels = {
        std::wstring(Tr(Msg::HubFilterAll)) + L" (" + std::to_wstring(m_hubNodes.size()) + L")",
        L"Docker (" + std::to_wstring(nDocker) + L")",
        L"WSL (" + std::to_wstring(nWsl) + L")",
        std::wstring(Tr(Msg::HubFilterLocal)) + L" (" + std::to_wstring(nLocal) + L")",
        L"SSH (" + std::to_wstring(nSsh) + L")",
        L"K8s (" + std::to_wstring(nK8s) + L")"
    };

    const float segH = std::floor(28 * s);
    m_ui.Choice(6020, D2D1::RectF(x0, y, x1, y + segH), catLabels, m_hubFilter);
    y += segH + std::floor(14 * s);

    // --- 4. Kaydirilabilir Kart Izgarasi Alani ---
    const float gridTop = y;
    m_r.PushClip(D2D1::RectF(a.left, gridTop, a.right, a.bottom));
    // Basligin arkasina kaymis kartlar, filtre/arama tiklarini yakalamasin.
    // Basis baslikta baslayip karta birakilirsa da (kart basligin arkasinda) tik sayilmasin.
    const PointerMask gridMask(m_in, m_in.my < gridTop || m_in.my >= a.bottom ||
                                     (m_in.py >= 0.0f && (m_in.py < gridTop || m_in.py >= a.bottom)));

    std::wstring low = m_quick;
    std::transform(low.begin(), low.end(), low.begin(), ::towlower);

    const float cardW = std::floor(354 * s);
    const float cardH = std::floor(76 * s);
    const float gap = std::floor(12 * s);
    const int perRow = std::max(1, (int)((x1 - x0 + gap) / (cardW + gap)));

    int shown = 0;
    float cx = x0, cy = gridTop - m_mainScroll;

    for (size_t idx = 0; idx < m_hubNodes.size(); ++idx) {
        const auto& node = m_hubNodes[idx];

        // Filtre kontrolu
        if (m_hubFilter == 1 && node.type != NodeType::DockerContainer) continue;
        if (m_hubFilter == 2 && node.type != NodeType::Wsl) continue;
        if (m_hubFilter == 3 && node.type != NodeType::Local) continue;
        if (m_hubFilter == 4 && node.type != NodeType::SshHost) continue;
        if (m_hubFilter == 5 && node.type != NodeType::K8sPod) continue;

        if (!low.empty()) {
            std::string hay = node.name + " " + node.path + " " + node.notes + " " + node.target + " " + node.status;
            std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
            std::string lowUtf8 = WideToUtf8(low);
            if (hay.find(lowUtf8) == std::string::npos) continue;
        }

        const D2D1_RECT_F card = D2D1::RectF(cx, cy, cx + cardW, cy + cardH);
        const bool hot = m_ui.Hot(card);

        // Rozet ve Renk Secimi
        uint32_t accent = theme::Ac();
        std::wstring badgeText = L"SYS";
        if (node.type == NodeType::DockerContainer) {
            accent = 0x0EA5E9; // Docker Sky Blue
            badgeText = L"DKR";
        } else if (node.type == NodeType::Wsl) {
            accent = 0x22C55E; // WSL Emerald Green
            badgeText = L"WSL";
        } else if (node.type == NodeType::Local) {
            accent = 0xA855F7; // Local Purple
            if (node.id.find("powershell") != std::string::npos || node.id.find("pwsh") != std::string::npos) badgeText = L"PS";
            else if (node.id.find("cmd") != std::string::npos) badgeText = L"CMD";
            else if (node.id.find("bash") != std::string::npos) badgeText = L"BASH";
            else badgeText = L"LOC";
        } else if (node.type == NodeType::SshHost) {
            accent = theme::Red;
            badgeText = L"SSH";
        } else if (node.type == NodeType::K8sPod) {
            accent = 0x326CE5; // Kubernetes Blue
            badgeText = L"K8S";
        }

        const bool pressed = m_ui.PressIn(card) && m_in.down;
        const bool isSelected = (!m_selHost.empty() && node.id == WideToUtf8(m_selHost));

        uint32_t cardBg = pressed ? theme::Sunken : (hot ? theme::Elevated : (isSelected ? theme::Elevated : theme::Surface));
        uint32_t cardBorder = (isSelected || pressed) ? theme::Ac() : (hot ? theme::BorderHi : theme::Border);
        m_r.FillRound(card, std::floor(7 * s), cardBg);
        m_r.Stroke(card, cardBorder, (isSelected || pressed) ? std::max(1.5f, std::floor(1.5f * s)) : std::max(1.0f, std::floor(1 * s)));

        // Sol Kapsul / Rozet
        const float ib = std::floor(44 * s);
        const D2D1_RECT_F ir = D2D1::RectF(card.left + std::floor(12 * s), card.top + (cardH - ib) * 0.5f,
                                           card.left + std::floor(12 * s) + ib, card.top + (cardH + ib) * 0.5f);
        m_r.FillRound(ir, std::floor(8 * s), accent, 0.20f);
        m_r.Stroke(ir, accent, std::max(1.0f, std::floor(1 * s)));
        m_r.Text(badgeText, ir, accent, 12.0f * s, Renderer::Align::Center, true);

        // Baslik
        const float textL = ir.right + std::floor(12 * s);
        const float btnAreaW = (node.type == NodeType::SshHost) ? std::floor(68 * s) : std::floor(36 * s);
        const float textR = card.right - btnAreaW - std::floor(10 * s);

        m_r.Text(Trunc(Utf8ToWide(node.name), 30),
                 D2D1::RectF(textL, card.top + std::floor(10 * s), textR, card.top + std::floor(30 * s)),
                 theme::TextHi, 13.0f * s, Renderer::Align::Left, true);

        // Alt Metin: Durum + Hedef / Not
        std::wstring sub;
        if (node.type == NodeType::DockerContainer) {
            sub = std::wstring(Tr(Msg::HubStatusRunning)) + L"  " + Utf8ToWide(node.notes);
        } else if (node.type == NodeType::Wsl) {
            sub = L"WSL2 " + std::wstring(Tr(Msg::HubStatusOnline)) + L"  " + Utf8ToWide(node.path);
        } else if (node.type == NodeType::Local) {
            sub = std::wstring(Tr(Msg::HubFilterLocal)) + L"  " + Utf8ToWide(node.path);
        } else if (node.type == NodeType::K8sPod) {
            // notes "Deployment | nginx [dosya.yaml]" bicimli; tur zaten icinde.
            sub = L"Kubernetes  " + Utf8ToWide(node.notes);
        } else {
            sub = Utf8ToWide(node.target);
            if (!node.notes.empty()) sub += L"  " + Utf8ToWide(node.notes);
        }

        // Kucuk yesil durum noktasi
        const float dotX = textL + std::floor(4 * s);
        const float dotY = card.top + std::floor(46 * s);
        const uint32_t dotCol = (node.status == "online" || node.status.find("Up") != std::string::npos || node.status == "running")
                                ? 0x22C55E : theme::TextDim;
        m_r.Disc(dotX, dotY, std::floor(3.0f * s), dotCol);

        m_r.Text(Trunc(sub, 38),
                 D2D1::RectF(dotX + std::floor(8 * s), card.top + std::floor(36 * s), textR, card.bottom - std::floor(8 * s)),
                 theme::TextDim, 11.0f * s, Renderer::Align::Left, false, true);

        // Sag Eylem Butonlari (Sadece ikonlarla yapilir, ferah ve modern)
        const float btnSize = std::floor(28 * s);
        const float btnY = card.top + (cardH - btnSize) * 0.5f;

        if (node.type == NodeType::SshHost) {
            const D2D1_RECT_F editR = D2D1::RectF(card.right - std::floor(10 * s) - btnSize, btnY,
                                                 card.right - std::floor(10 * s), btnY + btnSize);
            const D2D1_RECT_F connR = D2D1::RectF(editR.left - std::floor(6 * s) - btnSize, btnY,
                                                 editR.left - std::floor(6 * s), btnY + btnSize);

            if (m_ui.Button(7000 + (int)idx, connR, L"🚀", hot)) {
                LaunchNode(node);
                m_r.PopClip();
                m_r.PopClip();
                return;
            }
            if (m_ui.Button(7500 + (int)idx, editR, L"✏️")) {
                m_selHost = Utf8ToWide(node.id);
                m_hostDetail = true;
                m_hostDirty = false;
                m_mainScroll = 0.0f;
            }
        } else {
            const D2D1_RECT_F go = D2D1::RectF(card.right - std::floor(10 * s) - btnSize, btnY,
                                               card.right - std::floor(10 * s), btnY + btnSize);
            if (m_ui.Button(7000 + (int)idx, go, L">_", hot)) {
                LaunchNode(node);
                m_r.PopClip();
                m_r.PopClip();
                return;
            }
        }

        // Kutuya cift tiklama -> dogrudan baglan / calistir
        if (m_ui.DblClicked(card)) {
            LaunchNode(node);
            m_r.PopClip();
            m_r.PopClip();
            return;
        }

        // Kutuya tek tiklama -> sec ve odaklan
        if (m_ui.Clicked(card)) {
            m_selHost = Utf8ToWide(node.id);
            m_hostDirty = false;
        }


        ++shown;
        if (shown % perRow == 0) { cx = x0; cy += cardH + gap; }
        else cx += cardW + gap;
    }

    if (shown == 0) {
        m_r.Text(low.empty() ? Tr(Msg::HubNoSystems)
                             : TrText(L"No systems match the search.", L"Aramaya uyan sistem bulunamadı."),
                 D2D1::RectF(x0, cy, x1, cy + std::floor(30 * s)), theme::TextDim, 12.5f * s);
        cy += std::floor(40 * s);
    } else {
        if (shown % perRow != 0) cy += cardH + gap;
    }

    // --- 5. AI / MCP Entegrasyon Bilgilendirme Kutusu ---
    cy += std::floor(16 * s);
    const D2D1_RECT_F aiBox = D2D1::RectF(x0, cy, x1, cy + std::floor(86 * s));
    m_r.FillRound(aiBox, std::floor(8 * s), theme::Surface);
    m_r.Stroke(aiBox, theme::BorderHi, std::max(1.0f, std::floor(1 * s)));
    m_r.Fill(D2D1::RectF(aiBox.left, aiBox.top, aiBox.left + std::floor(4 * s), aiBox.bottom), theme::Ac());

    m_r.Text(TrText(L"Artificial Intelligence (AI) and MCP Server Integration", L"Yapay Zeka (AI) ve MCP Sunucusu Entegrasyonu"),
             D2D1::RectF(aiBox.left + std::floor(18 * s), aiBox.top + std::floor(12 * s),
                         aiBox.right - std::floor(180 * s), aiBox.top + std::floor(32 * s)),
             theme::TextHi, 13.0f * s, Renderer::Align::Left, true);

    m_r.Text(TrText(L"FullTerminal is a stdio-based high performance MCP server for Claude Desktop, Cursor and Antigravity.\n4 compact tools: ft_systems, ft_exec, ft_fs, ft_vault. Permissions and approvals: Settings > MCP.",
                    L"FullTerminal, Claude Desktop, Cursor ve Antigravity için stdio tabanlı yüksek hızlı bir MCP sunucusudur.\n4 kompakt araç: ft_systems, ft_exec, ft_fs, ft_vault. İzinler ve onay: Ayarlar > MCP."),
             D2D1::RectF(aiBox.left + std::floor(18 * s), aiBox.top + std::floor(34 * s),
                         aiBox.right - std::floor(180 * s), aiBox.bottom - std::floor(8 * s)),
             theme::TextMuted, 11.5f * s, Renderer::Align::Left, false, false, 1.0f, true);

    const D2D1_RECT_F copyBtn = D2D1::RectF(aiBox.right - std::floor(166 * s),
                                           aiBox.top + (aiBox.bottom - aiBox.top - std::floor(34 * s)) * 0.5f,
                                           aiBox.right - std::floor(16 * s),
                                           aiBox.top + (aiBox.bottom - aiBox.top + std::floor(34 * s)) * 0.5f);
    if (m_ui.Button(6030, copyBtn, TrText(L"Copy MCP Command", L"MCP Komutunu Kopyala"), true)) {
        wchar_t exePath[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        ClipboardSetText(m_hwnd, L"\"" + std::wstring(exePath) + L"\" --mcp");
        Toast(TrText(L"MCP command copied to clipboard!", L"MCP komutu panoya kopyalandı!"));
    }

    cy += std::floor(106 * s);

    const float used = (cy + m_mainScroll) - gridTop;
    const float viewH = a.bottom - gridTop;
    m_mainScroll = (used <= viewH) ? 0.0f : std::min(m_mainScroll, used - viewH);

    m_r.PopClip();
    m_r.PopClip();
}

void MainWindow::DrawHostDetail(const D2D1_RECT_F& a, Host& h) {
    const float s = m_lay.scale;
    const float pad = std::floor(22 * s);
    const float x0 = a.left + pad;
    const float x1 = std::min(a.right - pad, x0 + std::floor(640 * s));
    // Kucuk pencerede (Guake %50, yuksek DPI) Baglan/Kaydet/Sil asagida kalir: kayar.
    float y = a.top + std::floor(16 * s) - m_mainScroll;
    // Kayan satirlar alanin disinda (durum cubugunun altinda) tik almasin.
    const PointerMask outside(m_in, m_in.my < a.top || m_in.my >= a.bottom ||
                                    (m_in.py >= 0.0f && (m_in.py < a.top || m_in.py >= a.bottom)));

    // geri
    const std::wstring backLabel = L"‹  " + std::wstring(Tr(Msg::HostDetailBack));
    if (m_ui.Button(6100, D2D1::RectF(x0, y, x0 + std::floor(104 * s), y + std::floor(30 * s)),
                    backLabel.c_str())) {
        m_hostDetail = false;
        m_mainScroll = 0.0f;
        m_ui.SetFocus(ID_NONE);
        RefreshHubNodes(false);   // etiket/adres degisiklikleri kartlara yansisin
        return;
    }
    y += std::floor(44 * s);

    const uint32_t accent = h.production ? theme::Red : (h.accent ? h.accent : theme::Ac());
    const float ib = std::floor(44 * s);
    const D2D1_RECT_F ir = D2D1::RectF(x0, y, x0 + ib, y + ib);
    m_r.FillRound(ir, std::floor(10 * s), accent);
    const std::wstring initial(1, (wchar_t)towupper(h.Display().empty() ? L'?' : h.Display()[0]));
    m_r.Text(initial, ir, 0x0A0D11, 19.0f * s, Renderer::Align::Center, true);

    m_r.Text(h.Display(), D2D1::RectF(ir.right + std::floor(14 * s), y, x1, y + std::floor(26 * s)),
             theme::TextHi, 19.0f * s, Renderer::Align::Left, true);
    const std::wstring cmd = m_inv.BuildSshCommand(h);
    m_r.Text(cmd.empty() ? TrText(L"ssh.exe not found", L"ssh.exe bulunamadı") : Trunc(cmd, 78),
             D2D1::RectF(ir.right + std::floor(14 * s), y + std::floor(26 * s), x1, y + ib),
             theme::TextDim, 10.5f * s, Renderer::Align::Left, false, true);
    y += ib + std::floor(20 * s);

    const float rowH = std::floor(34 * s);
    const float gap = std::floor(14 * s);
    const float labelW = std::floor(118 * s);

    auto field = [&](const wchar_t* label, int id, std::wstring& value,
                     const wchar_t* ph, bool password = false) {
        m_r.Text(label, D2D1::RectF(x0, y, x0 + labelW, y + rowH), theme::TextMuted, 12.0f * s);
        if (m_ui.Field(id, D2D1::RectF(x0 + labelW, y, x1, y + rowH), value, ph, password)) m_hostDirty = true;
        y += rowH + std::floor(8 * s);
    };

    field(Tr(Msg::ColName), ID_H_LABEL, h.label, TrText(L"e.g. Prod web 1", L"örn. Prod web 1"));
    field(Tr(Msg::ColHostIp), ID_H_ADDR, h.address, TrText(L"10.0.0.15 or server.com", L"10.0.0.15 veya sunucu.com"));

    {
        m_r.Text(Tr(Msg::HostPort), D2D1::RectF(x0, y, x0 + labelW, y + rowH), theme::TextMuted, 12.0f * s);
        const float portW = std::floor(86 * s);
        if (m_ui.NumField(ID_H_PORT, D2D1::RectF(x0 + labelW, y, x0 + labelW + portW, y + rowH),
                          h.port, 1, 65535, L"22")) {
            m_hostDirty = true;
        }
        const float uw = std::floor(76 * s);
        m_r.Text(Tr(Msg::HostUsername), D2D1::RectF(x0 + labelW + portW + gap, y,
                                           x0 + labelW + portW + gap + uw, y + rowH),
                 theme::TextMuted, 12.0f * s);
        if (m_ui.Field(ID_H_USER, D2D1::RectF(x0 + labelW + portW + gap + uw, y, x1, y + rowH),
                       h.username, L"root")) m_hostDirty = true;
        y += rowH + std::floor(8 * s);
    }

    {
        m_r.Text(Tr(Msg::HostAuthKind), D2D1::RectF(x0, y, x0 + labelW, y + rowH), theme::TextMuted, 12.0f * s);
        int kind = (int)h.kind;
        const std::vector<std::wstring> kinds = { Tr(Msg::AuthPassword), Tr(Msg::AuthKey), Tr(Msg::AuthAgent) };
        if (m_ui.Choice(6110, D2D1::RectF(x0 + labelW, y, x0 + labelW + std::floor(250 * s), y + rowH),
                        kinds, kind)) {
            h.kind = (AuthKind)kind;
            m_hostDirty = true;
        }
        y += rowH + std::floor(8 * s);
    }

    if (h.kind == AuthKind::Password) {
        // DPAPI baska makinede/kullanicida sifrelenmis parolayi cozemediyse blob
        // korunur; kullanici yeni bir parola yazinca eskisi birakilir.
        const bool locked = h.secretLocked;
        const std::wstring before = h.password;
        m_r.Text(Tr(Msg::AuthPassword), D2D1::RectF(x0, y, x0 + labelW, y + rowH), theme::TextMuted, 12.0f * s);
        const float eyeW = std::floor(36 * s);
        const D2D1_RECT_F fieldR = D2D1::RectF(x0 + labelW, y, x1 - eyeW - std::floor(6 * s), y + rowH);
        const D2D1_RECT_F eyeR = D2D1::RectF(x1 - eyeW, y, x1, y + rowH);
        if (m_ui.Field(ID_H_PASS, fieldR, h.password,
                       locked ? TrText(L"could not decrypt on this machine - re-enter", L"bu makinede çözülemedi - yeniden gir") : TrText(L"prompted if left empty", L"boş bırakılırsa sorulur"), !m_showHostPassword)) {
            m_hostDirty = true;
        }
        if (m_ui.Button(6119, eyeR, m_showHostPassword ? L"🙈" : L"👁️")) {
            m_showHostPassword = !m_showHostPassword;
        }
        if (locked && h.password != before) { h.secretLocked = false; h.secretBlob.clear(); m_hostDirty = true; }
        y += rowH + std::floor(8 * s);
    }
    else if (h.kind == AuthKind::Key) {
        const float areaH = std::floor(90 * s);
        m_r.Text(Tr(Msg::IdentitiesPrivateKey), D2D1::RectF(x0, y + 2 * s, x0 + labelW, y + 20 * s), theme::TextMuted, 12.0f * s);
        if (m_ui.Button(6115, D2D1::RectF(x0, y + std::floor(26 * s), x0 + std::floor(96 * s), y + std::floor(54 * s)), Tr(Msg::ActionBrowse))) {
            std::wstring sel;
            if (PickKeyFile(m_hwnd, sel)) {
                std::wstring cont = ReadFileUtf8(sel);
                if (!cont.empty() && (cont.find(L"-----BEGIN") != std::wstring::npos || cont.find(L'\n') != std::wstring::npos)) h.keyPath = cont;
                else h.keyPath = sel;
                m_hostDirty = true;
            }
        }
        if (m_ui.TextArea(ID_H_KEY, D2D1::RectF(x0 + labelW, y, x1, y + areaH), h.keyPath,
                          TrText(L"-----BEGIN OPENSSH PRIVATE KEY-----\n...\n-----END OPENSSH PRIVATE KEY-----\n(or file path: C:\\Users\\...\\.ssh\\id_ed25519)",
                                 L"-----BEGIN OPENSSH PRIVATE KEY-----\n...\n-----END OPENSSH PRIVATE KEY-----\n(veya dosya yolu: C:\\Users\\...\\.ssh\\id_ed25519)"))) {
            m_hostDirty = true;
        }
        y += areaH + std::floor(8 * s);

        const float pubAreaH = std::floor(65 * s);
        m_r.Text(Tr(Msg::IdentitiesPublicKey), D2D1::RectF(x0, y + 2 * s, x0 + labelW, y + 20 * s), theme::TextMuted, 12.0f * s);
        if (m_ui.Button(6116, D2D1::RectF(x0, y + std::floor(26 * s), x0 + std::floor(96 * s), y + std::floor(54 * s)), Tr(Msg::ActionBrowse))) {
            std::wstring sel;
            if (PickPubOrCertFile(m_hwnd, sel)) {
                std::wstring cont = ReadFileUtf8(sel);
                if (!cont.empty() && (cont.find(L"ssh-") != std::wstring::npos || cont.find(L"ecdsa-") != std::wstring::npos || cont.find(L'\n') != std::wstring::npos)) h.publicKeyPath = cont;
                else h.publicKeyPath = sel;
                m_hostDirty = true;
            }
        }
        if (m_ui.TextArea(ID_H_PUBKEY, D2D1::RectF(x0 + labelW, y, x1, y + pubAreaH), h.publicKeyPath,
                          TrText(L"ssh-ed25519 AAAAC3...\n(optional, or file path: ...\\.ssh\\id_ed25519.pub)",
                                 L"ssh-ed25519 AAAAC3...\n(opsiyonel, veya dosya yolu: ...\\.ssh\\id_ed25519.pub)"))) {
            m_hostDirty = true;
        }
        y += pubAreaH + std::floor(8 * s);

        const float certAreaH = std::floor(65 * s);
        m_r.Text(Tr(Msg::IdentitiesCertificate), D2D1::RectF(x0, y + 2 * s, x0 + labelW, y + 20 * s), theme::TextMuted, 12.0f * s);
        if (m_ui.Button(6117, D2D1::RectF(x0, y + std::floor(26 * s), x0 + std::floor(96 * s), y + std::floor(54 * s)), Tr(Msg::ActionBrowse))) {
            std::wstring sel;
            if (PickPubOrCertFile(m_hwnd, sel)) {
                std::wstring cont = ReadFileUtf8(sel);
                if (!cont.empty() && (cont.find(L"ssh-") != std::wstring::npos || cont.find(L"-----BEGIN") != std::wstring::npos || cont.find(L'\n') != std::wstring::npos)) h.certPath = cont;
                else h.certPath = sel;
                m_hostDirty = true;
            }
        }
        if (m_ui.TextArea(ID_H_CERT, D2D1::RectF(x0 + labelW, y, x1, y + certAreaH), h.certPath,
                          TrText(L"ssh-rsa-cert-v01@openssh.com ... or -----BEGIN CERTIFICATE-----\n(optional, or file path: ...-cert.pub)",
                                 L"ssh-rsa-cert-v01@openssh.com ... veya -----BEGIN CERTIFICATE-----\n(opsiyonel, veya dosya yolu: ...-cert.pub)"))) {
            m_hostDirty = true;
        }
        y += certAreaH + std::floor(8 * s);
    }

    // kimlik secici
    {
        m_r.Text(TrText(L"Identity", L"Kimlik"), D2D1::RectF(x0, y, x0 + labelW, y + rowH), theme::TextMuted, 12.0f * s);
        std::wstring cur = Tr(Msg::IdentityNone);
        if (const Identity* id = m_inv.FindIdentity(h.identityId)) cur = id->name;
        if (m_ui.Button(6120, D2D1::RectF(x0 + labelW, y, x0 + labelW + std::floor(250 * s), y + rowH), cur)) {
            std::vector<std::wstring> items{ Tr(Msg::IdentityNone) };
            for (const auto& id : m_inv.identities()) items.push_back(id.name);
            POINT p{ (LONG)(x0 + labelW), (LONG)(y + rowH) };
            ClientToScreen(m_hwnd, &p);
            const int sel = ShowListMenu(p, items);
            if (sel == 0) { h.identityId.clear(); m_hostDirty = true; }
            else if (sel > 0) { h.identityId = m_inv.identities()[(size_t)sel - 1].id; m_hostDirty = true; }
        }
        y += rowH + std::floor(8 * s);
    }

    field(Tr(Msg::HostGroup), ID_H_GROUP, h.group, TrText(L"e.g. Production", L"örn. Production"));
    field(Tr(Msg::HostTags), ID_H_TAGS, h.tags, TrText(L"comma-separated", L"virgülle ayrılmış"));
    field(Tr(Msg::HostJumpHost), ID_H_JUMP, h.jumpHost, L"user@bastion[:port]");
    field(Tr(Msg::HostNotes), ID_H_NOTES, h.notes, TrText(L"free-form notes", L"serbest metin"));

    if (m_ui.Check(6130, D2D1::RectF(x0 + labelW, y, x1, y + rowH), h.production,
                   Tr(Msg::HostProduction))) m_hostDirty = true;
    y += rowH + std::floor(18 * s);

    const float bw = std::floor(116 * s), bh = std::floor(36 * s);
    const bool canConnect = !h.address.empty() && !cmd.empty();
    if (m_ui.Button(6140, D2D1::RectF(x0 + labelW, y, x0 + labelW + bw, y + bh), Tr(Msg::ActionConnect), true, false, canConnect)
        && canConnect) {
        ConnectHost(h);
    }
    if (m_ui.Button(6141, D2D1::RectF(x0 + labelW + bw + gap, y, x0 + labelW + bw * 2 + gap, y + bh), Tr(Msg::ActionSave))) {
        m_inv.Save(m_dataDir);
        m_hostDirty = false;
        RefreshHubNodes(false);
        Toast(Tr(Msg::ToastSaved));
    }
    if (m_ui.Button(6142, D2D1::RectF(x1 - bw, y, x1, y + bh), Tr(Msg::ActionDelete), false, true)) {
        m_inv.RemoveHost(h.id);
        m_selHost.clear();
        m_hostDetail = false;
        m_mainScroll = 0.0f;
        m_inv.Save(m_dataDir);
        RefreshHubNodes(false);   // silinen hostun karti kalmasin
        return;
    }
    y += bh + std::floor(12 * s);

    if (m_hostDirty) {
        m_r.Text(TrText(L"Unsaved changes", L"Kaydedilmemiş değişiklik var"),
                 D2D1::RectF(x0 + labelW, y, x1, y + std::floor(20 * s)), theme::Amber, 11.5f * s);
    }
    y += std::floor(24 * s);

    const float used = (y + m_mainScroll) - a.top;
    const float viewH = a.bottom - a.top;
    m_mainScroll = (used <= viewH) ? 0.0f : std::min(m_mainScroll, used - viewH);
}

void MainWindow::DrawIdentitiesScreen(const D2D1_RECT_F& a) {
    const float s = m_lay.scale;
    m_r.PushClip(a);

    const float pad = std::floor(22 * s);
    const float x0 = a.left + pad;
    const float x1 = std::min(a.right - pad, x0 + std::floor(640 * s));
    // Ekranin tamami kayar: liste uzarsa alttaki kimlikler de secilebilsin.
    float y = a.top + std::floor(20 * s) - m_mainScroll;
    const PointerMask outside(m_in, m_in.my < a.top || m_in.my >= a.bottom ||
                                    (m_in.py >= 0.0f && (m_in.py < a.top || m_in.py >= a.bottom)));

    m_r.Text(Tr(Msg::IdentitiesTitle), D2D1::RectF(x0, y, x1 - std::floor(340 * s), y + std::floor(28 * s)),
             theme::TextHi, 20.0f * s, Renderer::Align::Left, true);

    const float autoBtnW = std::floor(180 * s);
    const std::wstring importLabel = L"⚡ " + std::wstring(Tr(Msg::IdentitiesImportSsh));
    if (m_ui.Button(7001, D2D1::RectF(x1 - std::floor(130 * s) - autoBtnW - std::floor(8 * s), y, x1 - std::floor(138 * s), y + std::floor(32 * s)),
                    importLabel.c_str(), false)) {
        size_t n = m_inv.ImportSshKeysFromDisk();
        if (n > 0) {
            m_inv.Save(m_dataDir);
            if (!m_inv.identities().empty()) {
                m_selIdentity = m_inv.identities().back().id;
            }
            m_dirty = true;
            Toast(std::to_wstring(n) + L" " + TrText(L"SSH keys imported (from .ssh folder)", L"SSH anahtarı (.ssh klasöründen) içe aktarıldı"));
        } else {
            Toast(TrText(L"No new keys found (already saved)", L"Yeni anahtar bulunamadı (zaten kayıtlı)"));
        }
    }

    const std::wstring newIdLabel = L"+ " + std::wstring(Tr(Msg::ActionNewIdentity));
    if (m_ui.Button(7000, D2D1::RectF(x1 - std::floor(130 * s), y, x1, y + std::floor(32 * s)),
                    newIdLabel.c_str(), true)) {
        Identity& n = m_inv.AddIdentity();
        m_selIdentity = n.id;
    }
    y += std::floor(44 * s);

    // liste: en az gorunur alanin sonuna kadar, icerik uzunsa icerik kadar
    const float listW = std::floor(230 * s);
    const float listContentH = std::floor(12 * s) +
                               (float)m_inv.identities().size() * std::floor(42 * s);
    const float listBottom = std::max(y + listContentH,
                                      a.bottom - std::floor(20 * s) - m_mainScroll);
    const D2D1_RECT_F list = D2D1::RectF(x0, y, x0 + listW, listBottom);
    m_r.FillRound(list, std::floor(6 * s), theme::Surface);
    m_r.Stroke(list, theme::Border, std::max(1.0f, std::floor(1 * s)));

    float ly = list.top + std::floor(6 * s);
    int idx = 0;
    for (const auto& id : m_inv.identities()) {
        const D2D1_RECT_F r = D2D1::RectF(list.left + std::floor(4 * s), ly,
                                          list.right - std::floor(4 * s), ly + std::floor(40 * s));
        if (m_ui.Row(7100 + idx, r, id.id == m_selIdentity, theme::Violet)) m_selIdentity = id.id;
        m_r.Text(Trunc(id.name, 24),
                 D2D1::RectF(r.left + std::floor(12 * s), r.top + std::floor(4 * s),
                             r.right, r.top + std::floor(22 * s)),
                 id.id == m_selIdentity ? theme::TextHi : theme::Text, 12.0f * s);
        m_r.Text(id.username.empty() ? TrText(L"(no user)", L"(kullanıcı yok)") : id.username,
                 D2D1::RectF(r.left + std::floor(12 * s), r.top + std::floor(21 * s), r.right, r.bottom),
                 theme::TextDim, 10.5f * s, Renderer::Align::Left, false, true);
        ly += std::floor(42 * s);
        ++idx;
    }
    if (idx == 0) {
        m_r.Text(Tr(Msg::IdentitiesEmpty),
                 D2D1::RectF(list.left + std::floor(14 * s), list.top + std::floor(10 * s),
                             list.right, list.top + std::floor(34 * s)),
                 theme::TextDim, 12.0f * s);
    }

    // Kaydirma siniri: listenin ve formun alt kenarindan (kaydirilmamis) hesaplanir.
    auto clampScroll = [&](float contentBottom) {
        const float used = (contentBottom + m_mainScroll + std::floor(20 * s)) - a.top;
        const float viewH = a.bottom - a.top;
        m_mainScroll = (used <= viewH) ? 0.0f : std::min(m_mainScroll, used - viewH);
    };

    // form
    Identity* id = m_inv.FindIdentity(m_selIdentity);
    const float fx0 = list.right + std::floor(24 * s);
    if (!id) {
        m_r.Text(TrText(L"Identities are shared across multiple hosts.\nPasswords are saved encrypted using DPAPI, never stored in plain text.",
                        L"Kimlikler birden çok host tarafından paylaşılır.\nParolalar diske DPAPI ile şifreli yazılır, düz metin tutulmaz."),
                 D2D1::RectF(fx0, y, a.right - pad, y + std::floor(60 * s)),
                 theme::TextMuted, 12.5f * s, Renderer::Align::Left, false, false, 1.0f, true);
        clampScroll(std::max(y + listContentH, y + std::floor(60 * s)));
        m_r.PopClip();
        return;
    }

    const float fx1 = std::min(a.right - pad, fx0 + std::floor(420 * s));
    const float rowH = std::floor(34 * s);
    const float labelW = std::floor(110 * s);
    float fy = y;
    auto field = [&](const wchar_t* label, int fid, std::wstring& value, const wchar_t* ph, bool pw = false) {
        m_r.Text(label, D2D1::RectF(fx0, fy, fx0 + labelW, fy + rowH), theme::TextMuted, 12.0f * s);
        m_ui.Field(fid, D2D1::RectF(fx0 + labelW, fy, fx1, fy + rowH), value, ph, pw);
        fy += rowH + std::floor(8 * s);
    };

    field(Tr(Msg::ColName), ID_I_NAME, id->name, TrText(L"e.g. prod-deploy", L"örn. prod-deploy"));
    field(Tr(Msg::HostUsername), ID_I_USER, id->username, L"root");
    {
        m_r.Text(Tr(Msg::HostAuthKind), D2D1::RectF(fx0, fy, fx0 + labelW, fy + rowH), theme::TextMuted, 12.0f * s);
        int kind = (int)id->kind;
        const std::vector<std::wstring> kinds = { Tr(Msg::AuthPassword), Tr(Msg::AuthKey), Tr(Msg::AuthAgent) };
        if (m_ui.Choice(7200, D2D1::RectF(fx0 + labelW, fy, fx0 + labelW + std::floor(250 * s), fy + rowH),
                        kinds, kind)) id->kind = (AuthKind)kind;
        fy += rowH + std::floor(8 * s);
    }
    if (id->kind == AuthKind::Password) {
        const bool locked = id->secretLocked;
        const std::wstring before = id->password;
        m_r.Text(Tr(Msg::AuthPassword), D2D1::RectF(fx0, fy, fx0 + labelW, fy + rowH), theme::TextMuted, 12.0f * s);
        const float eyeW = std::floor(36 * s);
        const D2D1_RECT_F fieldR = D2D1::RectF(fx0 + labelW, fy, fx1 - eyeW - std::floor(6 * s), fy + rowH);
        const D2D1_RECT_F eyeR = D2D1::RectF(fx1 - eyeW, fy, fx1, fy + rowH);
        if (m_ui.Field(ID_I_PASS, fieldR, id->password, locked ? TrText(L"could not decrypt on this machine - re-enter", L"bu makinede çözülemedi - yeniden gir") : L"", !m_showIdPassword)) {
            // updated
        }
        if (m_ui.Button(7219, eyeR, m_showIdPassword ? L"🙈" : L"👁️")) {
            m_showIdPassword = !m_showIdPassword;
        }
        if (locked && id->password != before) { id->secretLocked = false; id->secretBlob.clear(); }
        fy += rowH + std::floor(8 * s);
    }
    else if (id->kind == AuthKind::Key) {
        const float areaH = std::floor(100 * s);
        const std::wstring privLabel = std::wstring(Tr(Msg::IdentitiesPrivateKey)) + L"*";
        m_r.Text(privLabel.c_str(), D2D1::RectF(fx0, fy + 2 * s, fx0 + labelW, fy + 20 * s), theme::TextMuted, 12.0f * s);
        if (m_ui.Button(7210, D2D1::RectF(fx0, fy + std::floor(26 * s), fx0 + std::floor(96 * s), fy + std::floor(54 * s)), Tr(Msg::ActionBrowse))) {
            std::wstring sel;
            if (PickKeyFile(m_hwnd, sel)) {
                std::wstring cont = ReadFileUtf8(sel);
                if (!cont.empty() && (cont.find(L"-----BEGIN") != std::wstring::npos || cont.find(L'\n') != std::wstring::npos)) id->keyPath = cont;
                else id->keyPath = sel;
            }
        }
        m_ui.TextArea(ID_I_KEY, D2D1::RectF(fx0 + labelW, fy, fx1, fy + areaH), id->keyPath,
                      TrText(L"-----BEGIN OPENSSH PRIVATE KEY-----\n...\n-----END OPENSSH PRIVATE KEY-----\n(or file path: C:\\Users\\...\\.ssh\\id_ed25519)",
                             L"-----BEGIN OPENSSH PRIVATE KEY-----\n...\n-----END OPENSSH PRIVATE KEY-----\n(veya dosya yolu: C:\\Users\\...\\.ssh\\id_ed25519)"));
        fy += areaH + std::floor(8 * s);

        const float pubAreaH = std::floor(75 * s);
        m_r.Text(Tr(Msg::IdentitiesPublicKey), D2D1::RectF(fx0, fy + 2 * s, fx0 + labelW, fy + 20 * s), theme::TextMuted, 12.0f * s);
        if (m_ui.Button(7212, D2D1::RectF(fx0, fy + std::floor(26 * s), fx0 + std::floor(96 * s), fy + std::floor(54 * s)), Tr(Msg::ActionBrowse))) {
            std::wstring sel;
            if (PickPubOrCertFile(m_hwnd, sel)) {
                std::wstring cont = ReadFileUtf8(sel);
                if (!cont.empty() && (cont.find(L"ssh-") != std::wstring::npos || cont.find(L"ecdsa-") != std::wstring::npos || cont.find(L'\n') != std::wstring::npos)) id->publicKeyPath = cont;
                else id->publicKeyPath = sel;
            }
        }
        m_ui.TextArea(ID_I_PUBKEY, D2D1::RectF(fx0 + labelW, fy, fx1, fy + pubAreaH), id->publicKeyPath,
                      TrText(L"ssh-ed25519 AAAAC3...\n(optional, or file path: ...\\.ssh\\id_ed25519.pub)",
                             L"ssh-ed25519 AAAAC3...\n(opsiyonel, veya dosya yolu: ...\\.ssh\\id_ed25519.pub)"));
        fy += pubAreaH + std::floor(8 * s);

        const float certAreaH = std::floor(75 * s);
        m_r.Text(Tr(Msg::IdentitiesCertificate), D2D1::RectF(fx0, fy + 2 * s, fx0 + labelW, fy + 20 * s), theme::TextMuted, 12.0f * s);
        if (m_ui.Button(7214, D2D1::RectF(fx0, fy + std::floor(26 * s), fx0 + std::floor(96 * s), fy + std::floor(54 * s)), Tr(Msg::ActionBrowse))) {
            std::wstring sel;
            if (PickPubOrCertFile(m_hwnd, sel)) {
                std::wstring cont = ReadFileUtf8(sel);
                if (!cont.empty() && (cont.find(L"ssh-") != std::wstring::npos || cont.find(L"-----BEGIN") != std::wstring::npos || cont.find(L'\n') != std::wstring::npos)) id->certPath = cont;
                else id->certPath = sel;
            }
        }
        m_ui.TextArea(ID_I_CERT, D2D1::RectF(fx0 + labelW, fy, fx1, fy + certAreaH), id->certPath,
                      TrText(L"ssh-rsa-cert-v01@openssh.com ... or -----BEGIN CERTIFICATE-----\n(optional, or file path: ...-cert.pub)",
                             L"ssh-rsa-cert-v01@openssh.com ... veya -----BEGIN CERTIFICATE-----\n(opsiyonel, veya dosya yolu: ...-cert.pub)"));
        fy += certAreaH + std::floor(8 * s);

        m_r.Text(Tr(Msg::IdentitiesPassphrase), D2D1::RectF(fx0, fy, fx0 + labelW, fy + rowH), theme::TextMuted, 12.0f * s);
        const float passEyeW = std::floor(36 * s);
        const D2D1_RECT_F passFieldR = D2D1::RectF(fx0 + labelW, fy, fx1 - passEyeW - std::floor(6 * s), fy + rowH);
        const D2D1_RECT_F passEyeR = D2D1::RectF(fx1 - passEyeW, fy, fx1, fy + rowH);
        m_ui.Field(ID_I_PASSPHRASE, passFieldR, id->passphrase, TrText(L"Key passphrase (optional)", L"Anahtar parolası (opsiyonel)"), !m_showIdPassphrase);
        if (m_ui.Button(7220, passEyeR, m_showIdPassphrase ? L"🙈" : L"👁️")) {
            m_showIdPassphrase = !m_showIdPassphrase;
        }
        fy += rowH + std::floor(8 * s);

        // SSH Anahtari Uret butonu
        const float genW = std::floor(180 * s), genH = std::floor(30 * s);
        const std::wstring genBtnText = L"+ " + std::wstring(Tr(Msg::IdentitiesGenKey));
        if (m_ui.Button(7250, D2D1::RectF(fx0 + labelW, fy, fx0 + labelW + genW, fy + genH), genBtnText.c_str())) {
            std::wstring keyName = id->name.empty() ? (L"id_ed25519_" + id->id.substr(0, 6)) : id->name;
            for (auto& c : keyName) if (c == L' ' || c == L':' || c == L'/') c = L'_';
            auto res = KeyGenService::GenerateKey(KeyAlgorithm::Ed25519, keyName, id->passphrase, L"FullTerminal:" + id->name);
            if (res.success) {
                std::wstring cont = ReadFileUtf8(res.privateKeyPath);
                id->keyPath = cont.empty() ? res.privateKeyPath : cont;
                std::wstring pubCont = ReadFileUtf8(res.publicKeyPath);
                id->publicKeyPath = pubCont.empty() ? res.publicKeyPath : pubCont;
                Toast(TrText(L"Ed25519 key generated and loaded into fields!", L"Ed25519 anahtarı üretildi ve alanlara yüklendi!"));
            } else {
                Toast(std::wstring(TrText(L"Error: ", L"Hata: ")) + res.error);
            }
        }
        fy += genH + std::floor(8 * s);
    }

    fy += std::floor(8 * s);
    const float bw = std::floor(116 * s), bh = std::floor(36 * s);
    if (m_ui.Button(7300, D2D1::RectF(fx0 + labelW, fy, fx0 + labelW + bw, fy + bh), Tr(Msg::ActionSave), true)) {
        if (id->kind == AuthKind::Key && id->keyPath.empty()) {
            Toast(TrText(L"Private Key (text or file path) is required!", L"Private Key (metin veya dosya yolu) zorunludur!"));
        } else {
            m_inv.Save(m_dataDir);
            Toast(Tr(Msg::ToastSaved));
        }
    }
    if (m_ui.Button(7301, D2D1::RectF(fx0 + labelW + bw + std::floor(14 * s), fy,
                                      fx0 + labelW + bw * 2 + std::floor(14 * s), fy + bh),
                    Tr(Msg::ActionDelete), false, true)) {
        m_inv.RemoveIdentity(id->id);
        m_selIdentity.clear();
        m_inv.Save(m_dataDir);
    }
    clampScroll(std::max(y + listContentH, fy + bh));

    m_r.PopClip();
}

// ---------------------------------------------------------------- ayarlar --

void MainWindow::DrawSettingsScreen(const D2D1_RECT_F& a) {
    const float s = m_lay.scale;
    m_r.PushClip(a);

    const float pad = std::floor(22 * s);
    const float x0 = a.left + pad;
    const float x1 = std::min(a.right - pad, x0 + std::floor(660 * s));
    const float rowH = std::floor(34 * s);
    const float labelW = std::floor(168 * s);
    float y = a.top + std::floor(20 * s);

    m_r.Text(Tr(Msg::SettingsTitle), D2D1::RectF(x0, y, x1, y + std::floor(30 * s)),
             theme::TextHi, 20.0f * s, Renderer::Align::Left, true);
    y += std::floor(42 * s);

    // kategori seridi
    {
        // Goruntu sirasi -> switch'teki kimlik. Dil (8), Visor (7).
        static const struct { Msg msg; int id; } tabs[] = {
            { Msg::TabAppearance, 0 }, { Msg::TabTerminal, 1 }, { Msg::TabShell, 2 }, { Msg::TabVisor, 7 },
            { Msg::TabLanguage, 8 }, { Msg::TabApplication, 3 }, { Msg::TabMcp, 4 }, { Msg::TabKubernetes, 5 }, { Msg::TabAbout, 6 },
        };
        float x = x0;
        for (const auto& tb : tabs) {
            const wchar_t* tabName = Tr(tb.msg);
            const float w = m_r.MeasureText(tabName, 12.5f * s, true) + std::floor(28 * s);
            const D2D1_RECT_F r = D2D1::RectF(x, y, x + w, y + std::floor(32 * s));
            const bool active = (m_settingsTab == tb.id);
            if (active) m_r.FillRound(r, std::floor(16 * s), theme::Ac(), 0.18f);
            else if (m_ui.Hot(r)) m_r.FillRound(r, std::floor(16 * s), theme::Elevated);
            m_r.Text(tabName, r, active ? theme::AcHi() : theme::TextMuted, 12.5f * s,
                     Renderer::Align::Center, active);
            if (ClickIn(r) && m_settingsTab != tb.id) {
                m_settingsTab = tb.id;
                m_mainScroll = 0.0f;
                m_ui.SetFocus(ID_NONE);
                if (m_captureHotkey) { m_captureHotkey = false; if (InQuake()) RegisterQuakeHotkey(false); }
            }
            x += w + std::floor(6 * s);
        }
        y += std::floor(46 * s);
    }

    // Sekme seridi sabit kalir, altindaki icerik kayar. Kayan icerik basligin
    // ustune cizilmez ve oradaki tiklari almaz.
    const float contentTop = y;
    y -= m_mainScroll;
    m_r.PushClip(D2D1::RectF(a.left, contentTop, a.right, a.bottom));
    // Alt kenarin altindaki (durum cubugu) basis/imlec de gizli satirlara ulasmasin.
    const PointerMask contentMask(m_in, m_in.my < contentTop || m_in.my >= a.bottom ||
                                        (m_in.py >= 0.0f && (m_in.py < contentTop || m_in.py >= a.bottom)));

    auto label = [&](const wchar_t* t) {
        m_r.Text(t, D2D1::RectF(x0, y, x0 + labelW, y + rowH), theme::TextMuted, 12.0f * s);
    };
    auto note = [&](const wchar_t* t) {
        const float h = std::floor(40 * s);
        m_r.Text(t, D2D1::RectF(x0, y, x1, y + h), theme::TextDim, 11.5f * s,
                 Renderer::Align::Left, false, false, 1.0f, true);
        y += h + std::floor(4 * s);
    };

    switch (m_settingsTab) {

    // ---------------------------------------------------------- gorunum ----
    case 0: {
        label(Tr(Msg::AccentColor));
        {
            int c = m_cfg.accentChoice;
            std::vector<std::wstring> items(theme::AccentNames, theme::AccentNames + 5);
            if (m_ui.Choice(8000, D2D1::RectF(x0 + labelW, y, x1, y + rowH), items, c)) {
                m_cfg.accentChoice = c;
                theme::SetAccent(theme::AccentChoices[c]);
                HICON old = m_icon;
                m_icon = MakeAppIcon(32, theme::Ac());
                if (m_icon) {
                    SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, (LPARAM)m_icon);
                    SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)m_icon);
                }
                if (m_trayVisible) AddTrayIcon();   // tepsi eski (yok edilecek) HICON'u tutmasin
                if (old) DestroyIcon(old);
            }
            y += rowH + std::floor(8 * s);
        }

        label(Tr(Msg::FontFamily));
        {
            // Yazilan ad ayri bir tamponda durur; yalnizca basariyla uygulanirsa
            // ayara gecer. Yoksa yanlis yazilmis bir ad kaydedilir ve her acilista
            // SetFontFamily bosuna basarisiz olurdu.
            if (!m_fontEditInit) {
                m_fontEdit = m_cfg.fontFamily.empty() ? m_r.Metrics().family : m_cfg.fontFamily;
                m_fontEditInit = true;
            }
            m_ui.Field(ID_S_FONT, D2D1::RectF(x0 + labelW, y, x1, y + rowH), m_fontEdit, L"Cascadia Mono");
            y += rowH + std::floor(4 * s);
            m_r.Text(L"Uygulanan: " + m_r.Metrics().family,
                     D2D1::RectF(x0 + labelW, y, x1, y + std::floor(18 * s)),
                     theme::TextDim, 10.5f * s, Renderer::Align::Left, false, true);
            y += std::floor(22 * s);
            if (m_ui.Button(8005, D2D1::RectF(x0 + labelW, y, x0 + labelW + std::floor(110 * s), y + std::floor(30 * s)),
                            Tr(Msg::ApplyFont))) {
                const std::wstring want = TrimWs(m_fontEdit);
                if (!m_r.SetFontFamily(want)) {
                    Toast(L"Font bulunamadi: \"" + want + L"\". Onceki font korundu.");
                } else {
                    m_cfg.fontFamily = want;
                    m_cfg.Save(m_dataDir);
                    Toast(L"Font uygulandi: " + m_r.Metrics().family);
                }
                SyncGridToArea();
            }
            y += std::floor(38 * s);
        }

        label(Tr(Msg::FontSize));
        {
            const float bw = std::floor(34 * s);
            if (m_ui.Button(8010, D2D1::RectF(x0 + labelW, y, x0 + labelW + bw, y + rowH), L"−")) {
                m_cfg.fontPt = std::max(6.0f, m_cfg.fontPt - 1.0f);
                m_r.SetFontSizePt(m_cfg.fontPt);
                SyncGridToArea();
            }
            wchar_t buf[16]; swprintf_s(buf, L"%.0f pt", m_cfg.fontPt);
            m_r.Text(buf, D2D1::RectF(x0 + labelW + bw, y, x0 + labelW + bw + std::floor(66 * s), y + rowH),
                     theme::TextHi, 12.5f * s, Renderer::Align::Center, true, true);
            if (m_ui.Button(8011, D2D1::RectF(x0 + labelW + bw + std::floor(66 * s), y,
                                              x0 + labelW + bw * 2 + std::floor(66 * s), y + rowH), L"+")) {
                m_cfg.fontPt = std::min(32.0f, m_cfg.fontPt + 1.0f);
                m_r.SetFontSizePt(m_cfg.fontPt);
                SyncGridToArea();
            }
            y += rowH + std::floor(8 * s);
        }

        label(Tr(Msg::Opacity));
        {
            const float bw = std::floor(32 * s);
            const float valW = std::floor(58 * s);
            // [-] butonu (5% azalt)
            if (m_ui.Button(8020, D2D1::RectF(x0 + labelW, y, x0 + labelW + bw, y + rowH), L"−")) {
                m_cfg.opacity = std::clamp(std::round((m_cfg.opacity - 0.05f) * 100.0f) / 100.0f, 0.10f, 1.0f);
                m_dirty = true;
                m_cfg.Save(m_dataDir);
            }
            int pct = (int)std::round(m_cfg.opacity * 100.0f);
            wchar_t buf[16]; swprintf_s(buf, L"%%%d", pct);
            const D2D1_RECT_F valRect = D2D1::RectF(x0 + labelW + bw, y, x0 + labelW + bw + valW, y + rowH);
            m_r.FillRound(valRect, std::floor(4 * s), theme::Elevated);
            m_r.Text(buf, valRect, theme::AcHi(), 12.5f * s, Renderer::Align::Center, true, true);

            // [+] butonu (5% artir)
            if (m_ui.Button(8021, D2D1::RectF(x0 + labelW + bw + valW, y,
                                              x0 + labelW + bw * 2 + valW, y + rowH), L"+")) {
                m_cfg.opacity = std::clamp(std::round((m_cfg.opacity + 0.05f) * 100.0f) / 100.0f, 0.10f, 1.0f);
                m_dirty = true;
                m_cfg.Save(m_dataDir);
            }

            // Hizli Hazir Ayar Butonlari (Presets)
            float px = x0 + labelW + bw * 2 + valW + std::floor(10 * s);
            struct Preset { int pct; const wchar_t* label; float w; };
            const Preset presets[] = {
                { 100, Tr(Msg::FullOpaque), std::floor(96 * s) },
                { 85,  L"%85",              std::floor(48 * s) },
                { 70,  L"%70",              std::floor(48 * s) },
                { 50,  L"%50",              std::floor(48 * s) },
                { 30,  L"%30",              std::floor(48 * s) },
            };
            for (int pi = 0; pi < 5; ++pi) {
                const D2D1_RECT_F pr = D2D1::RectF(px, y, px + presets[pi].w, y + rowH);
                const bool active = (pct == presets[pi].pct);
                if (m_ui.Button(8022 + pi, pr, presets[pi].label, active)) {
                    m_cfg.opacity = presets[pi].pct / 100.0f;
                    m_dirty = true;
                    m_cfg.Save(m_dataDir);
                }
                px += presets[pi].w + std::floor(6 * s);
            }

            y += rowH + std::floor(6 * s);
            note(Tr(Msg::NoteOpacity));
        }

        label(Tr(Msg::CursorStyle));
        {
            int style = m_cfg.cursorStyle == 3 ? 1 : (m_cfg.cursorStyle == 5 ? 2 : 0);
            const std::vector<std::wstring> styles = { Tr(Msg::CursorStyleBlock), Tr(Msg::CursorStyleUnderline), Tr(Msg::CursorStyleBar) };
            if (m_ui.Choice(8030, D2D1::RectF(x0 + labelW, y, x0 + labelW + std::floor(250 * s), y + rowH),
                            styles, style)) {
                m_cfg.cursorStyle = (style == 1) ? 3 : (style == 2 ? 5 : 0);
            }
            y += rowH + std::floor(8 * s);
        }
        m_ui.Check(8040, D2D1::RectF(x0 + labelW, y, x1, y + rowH), m_cfg.cursorBlink, Tr(Msg::CursorBlink));
        y += rowH + std::floor(8 * s);

        label(Tr(Msg::ColorPalette));
        {
            // Dokuz secenek tek seride sigmiyordu ("CyberpunkRetro CRT" ust uste
            // biniyordu); iki satira bolunur, tek secim korunur.
            const int cs = m_cfg.colorScheme;
            const std::vector<std::wstring> rowA = { L"Obsidian", L"Visor Dark", L"Dracula", L"Monokai", L"Solarized" };
            const std::vector<std::wstring> rowB = { L"Nord", L"Cyberpunk", L"Retro CRT", L"Custom" };
            int ia = (cs >= 0 && cs < 5) ? cs : -1;
            int ib = (cs >= 5 && cs < 9) ? cs - 5 : -1;
            if (m_ui.Choice(8060, D2D1::RectF(x0 + labelW, y, x1, y + rowH), rowA, ia)) {
                m_cfg.colorScheme = ia;
                ApplyColorScheme();
            }
            y += rowH + std::floor(4 * s);
            const float bw = std::floor((x1 - x0 - labelW) * 4.0f / 5.0f);
            if (m_ui.Choice(8061, D2D1::RectF(x0 + labelW, y, x0 + labelW + bw, y + rowH), rowB, ib)) {
                m_cfg.colorScheme = 5 + ib;
                ApplyColorScheme();
            }
            y += rowH + std::floor(8 * s);

            // 16 ANSI Canli Renk Kutucuklari Onizleme
            label(Tr(Msg::AnsiColors));
            const float chipSize = std::floor(18 * s);
            const float chipGap = std::floor(4 * s);
            float chipX = x0 + labelW;
            const float chipY = y + std::floor(7 * s);
            for (int i = 0; i < 16; ++i) {
                const D2D1_RECT_F cr = D2D1::RectF(chipX, chipY, chipX + chipSize, chipY + chipSize);
                m_r.FillRound(cr, std::floor(3 * s), theme::Ansi[i]);
                m_r.Stroke(cr, theme::Border, std::max(1.0f, std::floor(1 * s)));
                chipX += chipSize + chipGap;
            }
            y += rowH + std::floor(8 * s);

            // Ozel Renk Gami Secildiyse
            if (m_cfg.colorScheme == 8) {
                label(Tr(Msg::CustomColors));
                const float hw = std::floor(120 * s);
                m_ui.HexColorField(ID_S_CUSTOM_BG, D2D1::RectF(x0 + labelW, y, x0 + labelW + hw, y + rowH),
                                   m_cfg.customBgColor, L"#1e1e1e");
                m_ui.HexColorField(ID_S_CUSTOM_FG, D2D1::RectF(x0 + labelW + hw + std::floor(10 * s), y,
                                                               x0 + labelW + hw * 2 + std::floor(10 * s), y + rowH),
                                   m_cfg.customFgColor, L"#cccccc");
                if (m_ui.Button(8065, D2D1::RectF(x0 + labelW + hw * 2 + std::floor(20 * s), y, x1, y + rowH), Tr(Msg::ActionApply))) {
                    ApplyColorScheme();
                    Toast(Tr(Msg::ToastSaved));
                }
                y += rowH + std::floor(8 * s);
            }
        }

        label(Tr(Msg::VisorSettings));
        {
            const std::wstring visorBtnText = InQuake() ? (std::wstring(Tr(Msg::VisorSettings)) + L" (✓)") : std::wstring(Tr(Msg::VisorSettings));
            if (m_ui.Button(8059, D2D1::RectF(x0 + labelW, y, x0 + labelW + std::floor(200 * s), y + rowH),
                            visorBtnText.c_str())) {
                m_settingsTab = 7;
                m_mainScroll = 0.0f;
            }
            y += rowH + std::floor(8 * s);
        }
        break;
    }

    // ------------------------------------------------------------ visor ----
    case 7:
        DrawVisorSettings(x0, x1, labelW, rowH, y);
        break;


    // --------------------------------------------------------- terminal ----
    case 1: {
        label(Tr(Msg::ScrollbackLines));
        {
            m_ui.NumField(ID_S_SCROLLBACK, D2D1::RectF(x0 + labelW, y, x0 + labelW + std::floor(140 * s), y + rowH),
                          m_cfg.scrollbackLines, 100, 1000000, L"10000");
            y += rowH + std::floor(6 * s);
            note(Tr(Msg::NoteScrollback));
        }
        m_ui.Check(8100, D2D1::RectF(x0 + labelW, y, x1, y + rowH), m_cfg.copyOnSelect, Tr(Msg::CopyOnSelect));
        y += rowH + std::floor(4 * s);
        m_ui.Check(8101, D2D1::RectF(x0 + labelW, y, x1, y + rowH), m_cfg.bellSound, Tr(Msg::BellSound));
        y += rowH + std::floor(4 * s);
        m_ui.Check(8102, D2D1::RectF(x0 + labelW, y, x1, y + rowH), m_cfg.pasteGuard, Tr(Msg::PasteGuard));
        y += rowH + std::floor(8 * s);
        break;
    }

    // ------------------------------------------------------------ kabuk ----
    case 2: {
        label(Tr(Msg::DefaultProfile));
        {
            std::wstring cur = Tr(Msg::ProfileFirstFound);
            for (const auto& p : m_profiles) if (p.id == m_cfg.defaultProfile) cur = p.name;
            if (m_ui.Button(8200, D2D1::RectF(x0 + labelW, y, x0 + labelW + std::floor(280 * s), y + rowH), cur)) {
                std::vector<std::wstring> items;
                for (const auto& p : m_profiles) items.push_back(p.name);
                POINT pt{ (LONG)(x0 + labelW), (LONG)(y + rowH) };
                ClientToScreen(m_hwnd, &pt);
                const int sel = ShowListMenu(pt, items);
                if (sel >= 0) m_cfg.defaultProfile = m_profiles[(size_t)sel].id;
            }
            y += rowH + std::floor(16 * s);
        }
        m_ui.Caption(D2D1::RectF(x0, y, x1, y + std::floor(18 * s)), Tr(Msg::DiscoveredProfiles));
        y += std::floor(24 * s);
        for (const auto& p : m_profiles) {
            const D2D1_RECT_F r = D2D1::RectF(x0, y, x1, y + std::floor(34 * s));
            m_r.FillRound(r, std::floor(5 * s), theme::Surface);
            const D2D1_RECT_F badge = D2D1::RectF(r.left + std::floor(8 * s), r.top + std::floor(7 * s),
                                                  r.left + std::floor(58 * s), r.bottom - std::floor(7 * s));
            m_ui.Badge(badge, p.badge, p.accent ? p.accent : theme::Ac());
            m_r.Text(p.name, D2D1::RectF(badge.right + std::floor(10 * s), r.top, r.right - std::floor(10 * s), r.bottom),
                     theme::Text, 12.0f * s);
            m_r.Text(Trunc(p.exe, 46), D2D1::RectF(x0, r.top, r.right - std::floor(10 * s), r.bottom),
                     theme::TextDim, 10.0f * s, Renderer::Align::Right, false, true);
            y += std::floor(38 * s);
        }
        break;
    }

    // -------------------------------------------------------- uygulama ----
    case 3: {
        m_ui.Check(8300, D2D1::RectF(x0, y, x1, y + rowH), m_cfg.runInBackground, Tr(Msg::RunInBackground));
        y += rowH + std::floor(2 * s);
        note(Tr(Msg::NoteRunInBackground));

        m_ui.Check(8301, D2D1::RectF(x0, y, x1, y + rowH), m_cfg.minimizeToTray, Tr(Msg::MinimizeToTray));
        y += rowH + std::floor(4 * s);
        m_ui.Check(8302, D2D1::RectF(x0, y, x1, y + rowH), m_cfg.confirmClose, Tr(Msg::ConfirmClose));
        y += rowH + std::floor(4 * s);
        m_ui.Check(8303, D2D1::RectF(x0, y, x1, y + rowH), m_cfg.restoreSessions, Tr(Msg::RestoreSessions));
        y += rowH + std::floor(2 * s);
        note(Tr(Msg::NoteRestoreSessions));

        y += std::floor(8 * s);
        if (m_ui.Button(8310, D2D1::RectF(x0, y, x0 + std::floor(150 * s), y + std::floor(34 * s)),
                        Tr(Msg::OpenDataDir))) {
            ShellExecuteW(m_hwnd, L"open", m_dataDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        if (m_ui.Button(8311, D2D1::RectF(x0 + std::floor(162 * s), y,
                                          x0 + std::floor(300 * s), y + std::floor(34 * s)),
                        Tr(Msg::QuitApp), false, true)) {
            m_reallyQuit = true;
            PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
        }
        y += std::floor(44 * s);
        break;
    }

    // ------------------------------------------------------------- MCP ----
    case 4: {
        m_r.Text(TrText(L"Model Context Protocol (MCP)", L"Model Context Protocol (MCP)"),
                 D2D1::RectF(x0, y, x1, y + std::floor(24 * s)), theme::TextHi, 14.0f * s,
                 Renderer::Align::Left, true);
        y += std::floor(28 * s);
        note(TrText(L"FullTerminal.exe --mcp runs as an MCP server via stdio (ft_systems, ft_exec, ft_fs, ft_vault). Permissions below are re-read and applied on EVERY tool call; the server executes no tools unless enabled.",
                    L"FullTerminal.exe --mcp, stdio uzerinden bir MCP sunucusu olarak calisir (ft_systems, ft_exec, ft_fs, ft_vault). Asagidaki izinler HER arac cagrisinda yeniden okunur ve uygulanir; etkinlestirilmedikce sunucu hicbir araci calistirmaz."));

        if (m_ui.Check(8400, D2D1::RectF(x0, y, x1, y + rowH), m_cfg.mcpEnabled, TrText(L"Enable MCP server", L"MCP sunucusunu etkinleştir"))) {
            m_cfg.Save(m_dataDir);   // calisan sunucu ayari bir sonraki cagrida diskten okur
        }
        y += rowH + std::floor(10 * s);

        // Calisan sunucu (--mcp) ayarlari her arac cagrisinda diskten okur:
        // bir anahtar degisince hemen kaydet ki fark bir sonraki cagrida gecerli olsun.
        bool mcpSave = false;
        m_ui.Caption(D2D1::RectF(x0, y, x1, y + std::floor(18 * s)), TrText(L"TRANSPORT", L"TAŞIMA"));
        y += std::floor(24 * s);
        mcpSave |= m_ui.Check(8410, D2D1::RectF(x0 + std::floor(12 * s), y, x1, y + rowH), m_cfg.mcpStdio,
                              TrText(L"stdio  (FullTerminal.exe --mcp)", L"stdio  (FullTerminal.exe --mcp)"));
        y += rowH + std::floor(4 * s);
        mcpSave |= m_ui.Check(8411, D2D1::RectF(x0 + std::floor(12 * s), y, x1, y + rowH), m_cfg.mcpHttp,
                              TrText(L"Streamable HTTP  (localhost only, not yet implemented)", L"Streamable HTTP  (sadece localhost, henüz uygulanmadı)"));
        y += rowH + std::floor(6 * s);

        label(TrText(L"HTTP port", L"HTTP portu"));
        {
            m_ui.NumField(ID_S_MCPPORT, D2D1::RectF(x0 + labelW, y, x0 + labelW + std::floor(120 * s), y + rowH),
                          m_cfg.mcpPort, 1, 65535, L"8787");
            y += rowH + std::floor(8 * s);
        }
        label(TrText(L"Access token", L"Erişim anahtarı"));
        {
            m_ui.Field(ID_S_MCPTOKEN, D2D1::RectF(x0 + labelW, y, x1 - std::floor(90 * s), y + rowH),
                       m_cfg.mcpToken, TrText(L"disabled if empty", L"boş ise HTTP kapalı"), true);
            if (m_ui.Button(8420, D2D1::RectF(x1 - std::floor(80 * s), y, x1, y + rowH), TrText(L"Generate", L"Üret"))) {
                m_cfg.mcpToken = RandomToken();
                mcpSave = true;
                Toast(TrText(L"New access token generated", L"Yeni anahtar üretildi"));
            }
            y += rowH + std::floor(14 * s);
        }

        m_ui.Caption(D2D1::RectF(x0, y, x1, y + std::floor(18 * s)), TrText(L"SECURITY", L"GÜVENLİK"));
        y += std::floor(24 * s);
        mcpSave |= m_ui.Check(8430, D2D1::RectF(x0 + std::floor(12 * s), y, x1, y + rowH), m_cfg.mcpReadOnly,
                              TrText(L"Read-only mode: no command execution, file writing, or inventory change", L"Salt okunur mod: komut çalıştırma, dosya yazma ve envanter değişikliği yok"));
        y += rowH + std::floor(4 * s);
        mcpSave |= m_ui.Check(8431, D2D1::RectF(x0 + std::floor(12 * s), y, x1, y + rowH), m_cfg.mcpApproval,
                              TrText(L"Show confirmation dialog for each command / file write", L"Her komut / yazma için onay penceresi göster"));
        y += rowH + std::floor(4 * s);
        mcpSave |= m_ui.Check(8432, D2D1::RectF(x0 + std::floor(12 * s), y, x1, y + rowH), m_cfg.mcpAudit,
                              TrText(L"Keep audit trail (portable_data\\mcp-audit.log)", L"Denetim izi tut (portable_data\\mcp-audit.log)"));
        y += rowH + std::floor(10 * s);

        m_ui.Caption(D2D1::RectF(x0, y, x1, y + std::floor(18 * s)), TrText(L"TOOL PERMISSIONS", L"ARAÇ İZİNLERİ"));
        y += std::floor(24 * s);
        mcpSave |= m_ui.Check(8440, D2D1::RectF(x0 + std::floor(12 * s), y, x1, y + rowH), m_cfg.mcpAllowRun,
                              TrText(L"ft_exec  (run commands on local, WSL, Docker, SSH)", L"ft_exec  (yerel, WSL, Docker, SSH üzerinde komut çalıştırma)"));
        y += rowH + std::floor(4 * s);
        mcpSave |= m_ui.Check(8441, D2D1::RectF(x0 + std::floor(12 * s), y, x1, y + rowH), m_cfg.mcpAllowFiles,
                              TrText(L"ft_fs  (list, read files; write if read-only is off)", L"ft_fs  (dosya listeleme, okuma; salt okunur kapalıysa yazma)"));
        y += rowH + std::floor(4 * s);
        mcpSave |= m_ui.Check(8442, D2D1::RectF(x0 + std::floor(12 * s), y, x1, y + rowH), m_cfg.mcpAllowK8s,
                              TrText(L"Kubernetes targets  (ft_exec / ft_fs on k8s:... systems)", L"Kubernetes hedefleri  (k8s:... sistemlerinde ft_exec / ft_fs)"));
        y += rowH + std::floor(14 * s);
        if (mcpSave) m_cfg.Save(m_dataDir);

        // Claude Code yapilandirmasi
        m_ui.Caption(D2D1::RectF(x0, y, x1, y + std::floor(18 * s)), TrText(L"CONFIGURATION FOR CLAUDE CODE", L"CLAUDE CODE İÇİN YAPILANDIRMA"));
        y += std::floor(24 * s);
        {
            wchar_t exe[MAX_PATH]{};
            GetModuleFileNameW(nullptr, exe, MAX_PATH);
            std::wstring esc(exe);
            for (size_t i = 0; i < esc.size(); ++i) {
                if (esc[i] == L'\\') { esc.insert(i, 1, L'\\'); ++i; }
            }
            const std::wstring snippet =
                L"{ \"mcpServers\": { \"fullterminal\": {\n"
                L"    \"command\": \"" + esc + L"\",\n"
                L"    \"args\": [\"--mcp\"]\n} } }";
            const D2D1_RECT_F box = D2D1::RectF(x0, y, x1, y + std::floor(84 * s));
            m_r.FillRound(box, std::floor(5 * s), theme::Sunken);
            m_r.Stroke(box, theme::Border, std::max(1.0f, std::floor(1 * s)));
            m_r.Text(snippet, D2D1::RectF(box.left + std::floor(12 * s), box.top + std::floor(6 * s),
                                          box.right - std::floor(10 * s), box.bottom),
                     theme::Text, 10.5f * s, Renderer::Align::Left, false, true);
            y += std::floor(92 * s);
            if (m_ui.Button(8450, D2D1::RectF(x0, y, x0 + std::floor(150 * s), y + std::floor(32 * s)),
                            TrText(L"Copy to clipboard", L"Panoya kopyala"))) {
                if (ClipboardSetText(m_hwnd, snippet)) Toast(TrText(L"Configuration copied", L"Yapılandırma kopyalandı"));
            }
            y += std::floor(40 * s);
        }
        break;
    }

    // ---------------------------------------------------- kubernetes ----
    case 5: {
        m_r.Text(TrText(L"Kubernetes YAML and Cluster Management", L"Kubernetes YAML ve Küme Yönetimi"),
                 D2D1::RectF(x0, y, x1, y + std::floor(24 * s)), theme::TextHi, 14.0f * s,
                 Renderer::Align::Left, true);
        y += std::floor(28 * s);
        note(TrText(L"FullTerminal natively discovers Kubernetes YAML manifests and connects pods to terminal sessions. Convert existing Docker, WSL and local systems to standard Kubernetes YAML manifests in one click.",
                    L"FullTerminal, Kubernetes YAML manifestlerini doğrudan tanır ve podları birer terminal oturumuna bağlar. Mevcut Docker, WSL ve yerel sistemlerinizi tek tıkla standart Kubernetes YAML manifestine dönüştürebilir."));

        // Durum bilgisi
        const auto& manifests = K8sManager::Instance().Manifests();
        const auto& pods = K8sManager::Instance().Pods();
        wchar_t k8sStat[256];
        swprintf_s(k8sStat, L"%s: %zu  |  %s: %zu",
                   TrText(L"Loaded Manifests", L"Yüklü Manifest"), manifests.size(),
                   TrText(L"Discovered Pods", L"Keşfedilen Pod"), pods.size());
        m_r.Text(k8sStat, D2D1::RectF(x0, y, x1, y + std::floor(24 * s)), theme::AcHi(), 12.0f * s, Renderer::Align::Left, true);
        y += std::floor(30 * s);

        // Eklenen kubeconfig'ler yeni yerel terminallerde KUBECONFIG olur
        {
            const K8sManager& km = K8sManager::Instance();
            const size_t nCfg = km.KubeconfigCount();
            std::wstring kc = std::wstring(TrText(L"Kubeconfig: ", L"Kubeconfig: ")) + std::to_wstring(nCfg) + L" " + TrText(L"files", L"dosya");
            if (nCfg > 0) {
                const std::wstring ctx = km.KubeContextSummary();
                if (!ctx.empty()) kc += L"  |  " + ctx;
            } else {
                kc += L"  (" + std::wstring(TrText(L"add a YAML containing kubeconfig", L"kubeconfig içeren bir YAML ekleyin")) + L")";
            }
            m_r.Text(kc, D2D1::RectF(x0, y, x1, y + std::floor(22 * s)), theme::Text, 11.5f * s,
                     Renderer::Align::Left, false, true);
            y += std::floor(26 * s);
            m_ui.Check(8501, D2D1::RectF(x0, y, x1, y + rowH), m_cfg.k8sAutoEnv,
                       TrText(L"Automatically set KUBECONFIG in new terminals", L"Yeni terminallerde KUBECONFIG'i otomatik ayarla"));
            y += rowH + std::floor(2 * s);
            note(TrText(L"When opening local shell, WSL, Docker, and kubectl tabs, loaded kubeconfig files are provided as KUBECONFIG (paths as /mnt/... for WSL). SSH sessions are unaffected.",
                        L"Yerel kabuk, WSL, Docker ve kubectl sekmeleri açılırken eklenen kubeconfig dosyaları KUBECONFIG olarak verilir (WSL için /mnt/... yolları). SSH oturumları etkilenmez."));
        }

        // Otomatik export ayari
        m_ui.Check(8500, D2D1::RectF(x0, y, x1, y + rowH), m_cfg.k8sAutoExport,
                   TrText(L"Automatically generate Kubernetes YAML manifest whenever inventory changes", L"Envanter her değiştiğinde otomatik Kubernetes YAML manifesti üret"));
        y += rowH + std::floor(12 * s);

        // Aksiyon butonlari
        m_ui.Caption(D2D1::RectF(x0, y, x1, y + std::floor(18 * s)), TrText(L"YAML OPERATIONS", L"YAML İŞLEMLERİ"));
        y += std::floor(24 * s);

        const float btnW1 = std::floor(180 * s);
        const float btnW2 = std::floor(210 * s);
        const float btnW3 = std::floor(160 * s);
        const float btnH = std::floor(34 * s);

        const std::wstring addYamlLabel = L"+ " + std::wstring(TrText(L"Add YAML File", L"YAML Dosyası Ekle"));
        if (m_ui.Button(8510, D2D1::RectF(x0, y, x0 + btnW1, y + btnH), addYamlLabel.c_str())) {
            std::wstring file;
            if (PickYamlFile(m_hwnd, file)) {
                std::wstring ierr;
                if (K8sManager::Instance().ImportYamlFile(file, &ierr)) {
                    RefreshHubNodes();
                    const std::wstring ctx = K8sManager::Instance().KubeContextSummary();
                    Toast(ctx.empty() ? TrText(L"YAML file imported", L"YAML dosyası içe aktarıldı")
                                      : (std::wstring(TrText(L"YAML imported. In new terminals: ", L"YAML içe aktarıldı. Yeni terminallerde: ")) + ctx));
                } else {
                    Toast(std::wstring(TrText(L"Failed to import YAML: ", L"YAML içe aktarılamadı: ")) + (ierr.empty() ? std::wstring(TrText(L"no valid resources", L"geçerli kaynak yok")) : ierr));
                }
            }
        }

        const std::wstring expInfraLabel = L"📥 " + std::wstring(TrText(L"Export Entire Infrastructure", L"Tüm Altyapıyı Export Et"));
        if (m_ui.Button(8511, D2D1::RectF(x0 + btnW1 + std::floor(10 * s), y, x0 + btnW1 + btnW2 + std::floor(10 * s), y + btnH),
                        expInfraLabel.c_str(), true)) {
            std::wstring exportPath = m_dataDir + L"\\k8s\\fullterminal-export.yaml";
            if (K8sManager::Instance().ExportInventory(m_inv, m_hubNodes, exportPath)) {
                RefreshHubNodes();
                Toast(TrText(L"Kubernetes YAML generated: portable_data/k8s/fullterminal-export.yaml", L"Kubernetes YAML üretildi: portable_data/k8s/fullterminal-export.yaml"));
            } else {
                Toast(TrText(L"Export failed.", L"Export başarısız oldu."));
            }
        }

        const std::wstring openK8sLabel = L"📂 " + std::wstring(TrText(L"Open K8s Folder", L"K8s Klasörünü Aç"));
        if (m_ui.Button(8512, D2D1::RectF(x0 + btnW1 + btnW2 + std::floor(20 * s), y, x0 + btnW1 + btnW2 + btnW3 + std::floor(20 * s), y + btnH),
                        openK8sLabel.c_str())) {
            std::wstring k8sDir = m_dataDir + L"\\k8s";
            ShellExecuteW(m_hwnd, L"open", k8sDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        y += btnH + std::floor(18 * s);

        // Kubeconfig ve Context Yonetimi (Kullanici istegi: YAML ekleme, sag tik veya butonla context secimi & aktif etme)
        const auto& kconfigs = K8sManager::Instance().Kubeconfigs();
        if (!kconfigs.empty()) {
            m_ui.Caption(D2D1::RectF(x0, y, x1, y + std::floor(18 * s)), TrText(L"LOADED KUBECONFIG & CLUSTER CONNECTIONS", L"YÜKLÜ KUBECONFIG & KÜME BAĞLANTILARI"));
            y += std::floor(24 * s);

            for (size_t ki = 0; ki < kconfigs.size(); ++ki) {
                const auto& kc = kconfigs[ki];
                const bool isActiveCfg = (ki == 0);

                // Dosya adi
                size_t slash = kc.path.find_last_of(L"\\/");
                std::wstring fname = (slash != std::wstring::npos) ? kc.path.substr(slash + 1) : kc.path;

                const D2D1_RECT_F kr = D2D1::RectF(x0, y, x1, y + std::floor(44 * s));
                m_r.FillRound(kr, std::floor(5 * s), isActiveCfg ? 0x001B2E : theme::Surface);
                m_r.Stroke(kr, isActiveCfg ? 0x00E5FF : theme::Border, isActiveCfg ? 1.5f : 1.0f);

                // Badge: K8S
                const D2D1_RECT_F badge = D2D1::RectF(kr.left + std::floor(8 * s), kr.top + std::floor(8 * s),
                                                      kr.left + std::floor(58 * s), kr.bottom - std::floor(8 * s));
                m_ui.Badge(badge, L"☸️ K8S", isActiveCfg ? 0x00E5FF : 0x326CE5);

                // Dosya adi ve aktif context bilgisi
                std::wstring curCtx = Utf8ToWide(kc.currentContext.empty() ? WideToUtf8(TrText(L"(default)", L"(varsayılan)")) : kc.currentContext);
                std::wstring titleText = fname + L"   [ Context: " + curCtx + L" ]";
                if (isActiveCfg) titleText += L"  ● " + std::wstring(Tr(Msg::TunnelActiveBadge));

                m_r.Text(titleText, D2D1::RectF(badge.right + std::floor(10 * s), kr.top + std::floor(4 * s),
                                               kr.right - std::floor(260 * s), kr.bottom - std::floor(4 * s)),
                         isActiveCfg ? 0x00E5FF : theme::TextHi, 12.0f * s, Renderer::Align::Left, true);

                // Context Sec Butonu
                const float ctxBtnW = std::floor(140 * s);
                const float termBtnW = std::floor(100 * s);
                const D2D1_RECT_F ctxBtn = D2D1::RectF(kr.right - ctxBtnW - termBtnW - std::floor(12 * s),
                                                      kr.top + std::floor(6 * s),
                                                      kr.right - termBtnW - std::floor(12 * s),
                                                      kr.bottom - std::floor(6 * s));
                const D2D1_RECT_F termBtn = D2D1::RectF(kr.right - termBtnW - std::floor(6 * s),
                                                       kr.top + std::floor(6 * s),
                                                       kr.right - std::floor(6 * s),
                                                       kr.bottom - std::floor(6 * s));

                bool openCtxMenu = false;
                if (m_ui.Button(8540 + (int)ki * 2, ctxBtn, TrText(L"Select Context ▾", L"Context Seç ▾"), isActiveCfg)) {
                    openCtxMenu = true;
                }

                if (openCtxMenu) {
                    POINT pt{ (LONG)ctxBtn.left, (LONG)ctxBtn.bottom };
                    ClientToScreen(m_hwnd, &pt);

                    std::vector<std::wstring> cItems;
                    for (const auto& c : kc.contexts) {
                        std::wstring label = Utf8ToWide(c.name);
                        if (c.name == kc.currentContext) label = L"✓  " + label + L"  (" + std::wstring(Tr(Msg::TunnelActiveBadge)) + L")";
                        cItems.push_back(label);
                    }
                    if (cItems.empty()) {
                        cItems.push_back(TrText(L"Default Context", L"Varsayılan Context"));
                    }
                    const int cSel = ShowListMenu(pt, cItems);
                    if (cSel >= 0 && (size_t)cSel < kc.contexts.size()) {
                        K8sManager::Instance().SetActiveContext(ki, kc.contexts[cSel].name);
                        Toast(std::wstring(TrText(L"Kubernetes active context changed: ", L"Kubernetes aktif context değiştirildi: ")) + Utf8ToWide(kc.contexts[cSel].name));
                        m_dirty = true;
                    }
                }

                // Terminal Butonu
                const std::wstring termLabel = L"🚀 " + std::wstring(Tr(Msg::TabTerminal));
                if (m_ui.Button(8540 + (int)ki * 2 + 1, termBtn, termLabel.c_str(), false, true)) {
                    K8sManager::Instance().SetActiveKubeconfig(ki);
                    NewK8sTab();
                    m_r.PopClip();
                    return;
                }

                y += std::floor(48 * s);
            }
            y += std::floor(10 * s);
        }

        // Pod listesi
        if (!pods.empty()) {
            m_ui.Caption(D2D1::RectF(x0, y, x1, y + std::floor(18 * s)), TrText(L"DISCOVERED PODS", L"KEŞFEDİLEN PODLAR"));
            y += std::floor(24 * s);
            for (size_t pi = 0; pi < pods.size(); ++pi) {
                const auto& pod = pods[pi];
                const D2D1_RECT_F pr = D2D1::RectF(x0, y, x1, y + std::floor(36 * s));
                m_r.FillRound(pr, std::floor(5 * s), theme::Surface);
                const D2D1_RECT_F badge = D2D1::RectF(pr.left + std::floor(8 * s), pr.top + std::floor(8 * s),
                                                      pr.left + std::floor(58 * s), pr.bottom - std::floor(8 * s));
                m_ui.Badge(badge, L"K8S", 0x326CE5);
                m_r.Text(Utf8ToWide(pod.name) + L" (" + Utf8ToWide(pod.ns) + L")",
                         D2D1::RectF(badge.right + std::floor(10 * s), pr.top, pr.right - std::floor(100 * s), pr.bottom),
                         theme::Text, 12.0f * s);
                const D2D1_RECT_F shBtn = D2D1::RectF(pr.right - std::floor(90 * s), pr.top + std::floor(4 * s),
                                                      pr.right - std::floor(8 * s), pr.bottom - std::floor(4 * s));
                // Komut K8sManager'dan: tur (pod/deploy/...) ve tirnaklama orada dogrulaniyor.
                // Elle "kubectl exec <ad>" kurmak Deployment'ta calismaz, YAML'daki adla
                // arguman enjeksiyonuna da acikti.
                const std::string execCmd = K8sManager::ExecCommand(pod);
                if (m_ui.Button(8520 + (int)pi, shBtn, Tr(Msg::TabTerminal), false, false, !execCmd.empty())) {
                    ConnectionNode cnode;
                    cnode.id = "k8s:" + pod.ns + "/" + pod.kind + "/" + pod.name;
                    cnode.name = pod.name;
                    cnode.type = NodeType::K8sPod;
                    cnode.target = execCmd;
                    LaunchNode(cnode);
                    m_r.PopClip();
                    return;
                }
                y += std::floor(40 * s);
            }
        }
        break;
    }

    // -------------------------------------------------------- hakkinda ----
    case 6: {
        auto row = [&](const wchar_t* k, const std::wstring& v) {
            m_r.Text(k, D2D1::RectF(x0, y, x0 + labelW, y + std::floor(26 * s)), theme::TextMuted, 12.0f * s);
            m_r.Text(v, D2D1::RectF(x0 + labelW, y, x1, y + std::floor(26 * s)),
                     theme::Text, 12.0f * s, Renderer::Align::Left, false, true);
            y += std::floor(28 * s);
        };
        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);

        row(TrText(L"Version", L"Sürüm"), L"0.1.0  (M0)");
        row(TrText(L"Rendering", L"Çizim"), L"D3D11 + DirectWrite + DirectComposition");
        row(TrText(L"Font", L"Font"), m_r.Metrics().family);
        row(L"DPI", std::to_wstring(m_dpi));
        row(L"ConPTY", ConPty::Available() ? TrText(L"available", L"var") : TrText(L"not available", L"yok"));
        {
            const std::wstring ssh = FindSshExe();
            row(L"ssh.exe", ssh.empty() ? TrText(L"not found", L"bulunamadı") : ssh);
        }
        row(TrText(L"Data directory", L"Veri klasörü"), m_dataDir);
        row(TrText(L"Executable", L"Çalıştırılabilir"), exe);
        y += std::floor(12 * s);
        m_r.Text(TrText(L"Native C++, zero UI framework. Single portable executable, zero external DLL dependencies.",
                        L"Native C++, hiçbir UI çatısı yok. Tek taşınabilir exe, harici DLL sıfır."),
                 D2D1::RectF(x0, y, x1, y + std::floor(40 * s)), theme::TextDim, 11.5f * s,
                 Renderer::Align::Left, false, false, 1.0f, true);
        y += std::floor(46 * s);
        break;
    }

    // ----------------------------------------------------------- dil / i18n ----
    case 8: {
        m_r.Text(Tr(Msg::LangSelectTitle),
                 D2D1::RectF(x0, y, x1, y + std::floor(24 * s)), theme::TextHi, 14.0f * s,
                 Renderer::Align::Left, true);
        y += std::floor(28 * s);
        note(Tr(Msg::LangSelectDesc));
        y += std::floor(10 * s);

        const auto& allLangs = I18n::Languages();
        const float colGap = std::floor(12 * s);
        const float rowGap = std::floor(10 * s);
        const int cols = 4;
        const float cardW = std::floor((x1 - x0 - colGap * (cols - 1)) / cols);
        const float cardH = std::floor(50 * s);

        for (size_t i = 0; i < allLangs.size(); ++i) {
            const auto& item = allLangs[i];
            const int col = (int)(i % cols);
            const int row = (int)(i / cols);
            const float cx = x0 + col * (cardW + colGap);
            const float cy = y + row * (cardH + rowGap);
            const D2D1_RECT_F cr = D2D1::RectF(cx, cy, cx + cardW, cy + cardH);

            const bool isCur = (I18n::CurrentLang() == item.id);
            const bool hot = m_ui.Hot(cr);

            if (isCur) {
                m_r.FillRound(cr, std::floor(8 * s), theme::Ac(), 0.22f);
                m_r.Stroke(cr, theme::AcHi(), std::max(1.5f, std::floor(1.8f * s)));
            } else if (hot) {
                m_r.FillRound(cr, std::floor(8 * s), theme::Elevated);
                m_r.Stroke(cr, theme::BorderHi, 1.0f);
            } else {
                m_r.FillRound(cr, std::floor(8 * s), theme::Surface);
                m_r.Stroke(cr, theme::Border, 1.0f);
            }

            // Badge sol tarafta (orn: [EN], [TR], [RU], [UK])
            const D2D1_RECT_F badgeRect = D2D1::RectF(cr.left + std::floor(10 * s),
                                                     cr.top + std::floor(10 * s),
                                                     cr.left + std::floor(50 * s),
                                                     cr.bottom - std::floor(10 * s));
            m_ui.Badge(badgeRect, item.badge, isCur ? theme::AcHi() : (hot ? theme::TextHi : theme::TextDim));

            // Dil adi
            const D2D1_RECT_F textRect = D2D1::RectF(badgeRect.right + std::floor(10 * s),
                                                    cr.top,
                                                    cr.right - (isCur ? std::floor(28 * s) : std::floor(8 * s)),
                                                    cr.bottom);
            m_r.Text(item.name, textRect, isCur ? theme::TextHi : (hot ? theme::TextHi : theme::Text),
                     12.5f * s, Renderer::Align::Left, isCur);

            // Secili onay tiki
            if (isCur) {
                m_r.Text(L"✓", D2D1::RectF(cr.right - std::floor(26 * s), cr.top, cr.right - std::floor(6 * s), cr.bottom),
                         theme::AcHi(), 14.0f * s, Renderer::Align::Center, true);
            }

            if (ClickIn(cr) && !isCur) {
                I18n::SetLanguage(item.id);
                m_cfg.language = I18n::CurrentCode();
                m_cfg.Save(m_dataDir);
                Toast(Tr(Msg::ToastLangChanged));
                m_dirty = true;
            }
        }

        const int numRows = (int)((allLangs.size() + cols - 1) / cols);
        y += numRows * (cardH + rowGap) + std::floor(16 * s);
        break;
    }
    }

    // kaydet
    y += std::floor(10 * s);
    if (m_ui.Button(8900, D2D1::RectF(x0, y, x0 + std::floor(160 * s), y + std::floor(36 * s)),
                    Tr(Msg::ActionSave), true)) {
        m_cfg.Save(m_dataDir);
        Toast(Tr(Msg::ToastSaved));
    }
    y += std::floor(46 * s);

    const float used = (y + m_mainScroll) - contentTop;
    const float viewH = a.bottom - contentTop;
    m_mainScroll = (used <= viewH) ? 0.0f : std::min(m_mainScroll, used - viewH);

    m_r.PopClip();   // icerik
    m_r.PopClip();
}

} // namespace ft
