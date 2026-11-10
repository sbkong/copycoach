// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include <QWidget>

#include <cstddef>
#include <vector>

class QAbstractButton;
class QLabel;
class QPushButton;
class QSlider;
class QToolButton;
class QVBoxLayout;

namespace cc::ui
{

class Theme;

/// The stem mixer dock (components.md "스템 믹서", ia.md section 7: the rows hold controls only, the
/// separation status lives in the status bar). Before separation: one [스템 분리…] button. After: a row per
/// stem with its colour, mute, solo and a volume slider; the shell forwards the changes to the audio service.
class StemMixerPanel final : public QWidget
{
    Q_OBJECT
public:
    explicit StemMixerPanel(const Theme &theme, QWidget *parent = nullptr);

    /// Rows in the given order (separation::stemNames order with the user-facing names); empty = the button.
    void setStems(const QStringList &names);
    void clear();
    [[nodiscard]] bool hasStems() const;
    [[nodiscard]] std::size_t stemCount() const;
    /// While a separation job runs the button is disabled.
    void setBusy(bool busy);

    void setMuted(std::size_t stem, bool muted);
    void setSolo(std::size_t stem, bool solo);
    void toggleMute(std::size_t stem);
    void toggleSolo(std::size_t stem);
    [[nodiscard]] bool isMuted(std::size_t stem) const;
    [[nodiscard]] bool isSolo(std::size_t stem) const;
    [[nodiscard]] QAbstractButton *separateButton() const;

signals:
    void separateRequested();
    void muteChanged(std::size_t stem, bool muted);
    void soloChanged(std::size_t stem, bool solo);
    void gainChanged(std::size_t stem, float gain);

private:
    struct Row {
        QWidget *widget = nullptr;
        QToolButton *mute = nullptr;
        QToolButton *solo = nullptr;
        QSlider *gain = nullptr;
    };
    [[nodiscard]] Row makeRow(std::size_t index, const QString &name);

    const Theme *m_theme;
    QLabel *m_empty;
    QPushButton *m_separate;
    QVBoxLayout *m_rowsLayout;
    std::vector<Row> m_rows;
};

} // namespace cc::ui
