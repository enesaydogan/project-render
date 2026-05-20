#include "ArchColorDialog.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>

namespace {

double ClampChannel(double value)
{
    return std::clamp(value, 0.0, 255.0) / 255.0;
}

double NormalizedHue(const QColor &color, double fallbackHue)
{
    float h = 0.0f;
    float s = 0.0f;
    float v = 0.0f;
    color.getHsvF(&h, &s, &v);
    return h >= 0.0 ? h : fallbackHue;
}

class ColorPlane final : public QWidget
{
public:
    explicit ColorPlane(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumSize(260, 260);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setValues(double hue, double saturation, double value)
    {
        const bool hueChanged = std::abs(m_hue - hue) > 1.0e-6;
        m_hue = std::clamp(hue, 0.0, 1.0);
        m_saturation = std::clamp(saturation, 0.0, 1.0);
        m_value = std::clamp(value, 0.0, 1.0);
        if (hueChanged) {
            m_cached = QImage();
        }
        update();
    }

    void setChangedCallback(std::function<void(double, double)> callback)
    {
        m_changed = std::move(callback);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QSize targetSize = size();
        if (m_cached.size() != targetSize) {
            m_cached = QImage(targetSize, QImage::Format_RGB32);
            for (int y = 0; y < targetSize.height(); ++y) {
                const double value = 1.0 - static_cast<double>(y) /
                                                std::max(1, targetSize.height() - 1);
                for (int x = 0; x < targetSize.width(); ++x) {
                    const double saturation =
                        static_cast<double>(x) / std::max(1, targetSize.width() - 1);
                    m_cached.setPixelColor(
                        x, y, QColor::fromHsvF(m_hue, saturation, value));
                }
            }
        }

        painter.drawImage(rect(), m_cached);
        painter.setPen(QColor(18, 18, 18));
        painter.drawRect(rect().adjusted(0, 0, -1, -1));

        const QPointF cursor(m_saturation * (width() - 1),
                             (1.0 - m_value) * (height() - 1));
        painter.setPen(QPen(Qt::black, 3.0));
        painter.drawEllipse(cursor, 6.0, 6.0);
        painter.setPen(QPen(Qt::white, 1.5));
        painter.drawEllipse(cursor, 6.0, 6.0);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        updateFromMouse(event->position());
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if ((event->buttons() & Qt::LeftButton) != 0) {
            updateFromMouse(event->position());
        }
    }

private:
    void updateFromMouse(const QPointF &pos)
    {
        const double saturation =
            std::clamp(pos.x() / std::max(1, width() - 1), 0.0, 1.0);
        const double value =
            std::clamp(1.0 - pos.y() / std::max(1, height() - 1), 0.0, 1.0);
        if (m_changed) {
            m_changed(saturation, value);
        }
    }

    double m_hue = 0.0;
    double m_saturation = 0.0;
    double m_value = 1.0;
    QImage m_cached;
    std::function<void(double, double)> m_changed;
};

class HueStrip final : public QWidget
{
public:
    explicit HueStrip(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedWidth(24);
        setMinimumHeight(260);
    }

    void setHue(double hue)
    {
        m_hue = std::clamp(hue, 0.0, 1.0);
        update();
    }

    void setChangedCallback(std::function<void(double)> callback)
    {
        m_changed = std::move(callback);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        for (int y = 0; y < height(); ++y) {
            const double hue =
                std::clamp(static_cast<double>(y) / std::max(1, height() - 1),
                           0.0, 1.0);
            painter.setPen(QColor::fromHsvF(hue, 1.0, 1.0));
            painter.drawLine(0, y, width(), y);
        }
        painter.setPen(QColor(18, 18, 18));
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
        const int y = static_cast<int>(m_hue * (height() - 1));
        painter.setPen(QPen(Qt::white, 2.0));
        painter.drawLine(0, y, width(), y);
        painter.setPen(QPen(Qt::black, 1.0));
        painter.drawLine(0, y + 2, width(), y + 2);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        updateFromMouse(event->position().y());
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if ((event->buttons() & Qt::LeftButton) != 0) {
            updateFromMouse(event->position().y());
        }
    }

private:
    void updateFromMouse(double y)
    {
        const double hue = std::clamp(y / std::max(1, height() - 1), 0.0, 1.0);
        if (m_changed) {
            m_changed(hue);
        }
    }

    double m_hue = 0.0;
    std::function<void(double)> m_changed;
};

class Swatch final : public QFrame
{
public:
    explicit Swatch(QWidget *parent = nullptr)
        : QFrame(parent)
    {
        setFixedSize(96, 28);
        setFrameShape(QFrame::StyledPanel);
    }

    void setColor(const QColor &color)
    {
        setStyleSheet(QStringLiteral("background-color:%1; border:1px solid #555;")
                          .arg(color.name(QColor::HexRgb)));
    }
};

class VariationStrip final : public QWidget
{
public:
    enum class Mode {
        Hue,
        Saturation,
        Value
    };

    explicit VariationStrip(Mode mode, QWidget *parent = nullptr)
        : QWidget(parent), m_mode(mode)
    {
        setMinimumHeight(26);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setBaseColor(const QColor &color, double fallbackHue)
    {
        float h = 0.0f;
        float s = 0.0f;
        float v = 0.0f;
        color.getHsvF(&h, &s, &v);
        m_hue = h >= 0.0f ? h : static_cast<float>(fallbackHue);
        m_saturation = s;
        m_value = v;
        update();
    }

    void setChangedCallback(std::function<void(const QColor &)> callback)
    {
        m_changed = std::move(callback);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        const int swatches = 8;
        const int gap = 2;
        const int cellWidth =
            std::max(1, (width() - gap * (swatches - 1)) / swatches);

        for (int i = 0; i < swatches; ++i) {
            const QRect rect(i * (cellWidth + gap), 0, cellWidth, height());
            painter.fillRect(rect, colorAt(i, swatches));
        }

        painter.setPen(QColor(42, 42, 42));
        painter.drawRect(this->rect().adjusted(0, 0, -1, -1));
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        chooseFromPosition(event->position().x());
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if ((event->buttons() & Qt::LeftButton) != 0) {
            chooseFromPosition(event->position().x());
        }
    }

private:
    QColor colorAt(int index, int count) const
    {
        const double t = count > 1
                             ? static_cast<double>(index) /
                                   static_cast<double>(count - 1)
                             : 0.0;
        if (m_mode == Mode::Hue) {
            double hue = m_hue + (t - 0.5) * 0.28;
            hue = hue - std::floor(hue);
            return QColor::fromHsvF(hue,
                                    std::clamp<double>(m_saturation, 0.18, 1.0),
                                    std::clamp<double>(m_value, 0.35, 1.0));
        }
        if (m_mode == Mode::Saturation) {
            return QColor::fromHsvF(m_hue,
                                    t,
                                    std::clamp<double>(m_value, 0.35, 1.0));
        }
        return QColor::fromHsvF(m_hue,
                                std::clamp<double>(m_saturation, 0.0, 1.0),
                                std::clamp(0.22 + t * 0.78, 0.0, 1.0));
    }

    void chooseFromPosition(double x)
    {
        const int swatches = 8;
        const int index = std::clamp(
            static_cast<int>((x / std::max(1, width())) * swatches),
            0, swatches - 1);
        if (m_changed) {
            m_changed(colorAt(index, swatches));
        }
    }

    Mode m_mode = Mode::Hue;
    float m_hue = 0.0f;
    float m_saturation = 0.0f;
    float m_value = 1.0f;
    std::function<void(const QColor &)> m_changed;
};

struct ChannelRow {
    QSlider *slider = nullptr;
    QDoubleSpinBox *spin = nullptr;
};

ChannelRow CreateChannelRow(QGridLayout *layout,
                            int row,
                            const QString &label,
                            double minValue,
                            double maxValue,
                            int decimals,
                            const QString &suffix = {})
{
    auto *name = new QLabel(label);
    auto *slider = new QSlider(Qt::Horizontal);
    slider->setRange(0, 1000);
    slider->setFixedWidth(230);

    auto *spin = new QDoubleSpinBox();
    spin->setRange(minValue, maxValue);
    spin->setDecimals(decimals);
    spin->setSingleStep(decimals == 0 ? 100.0 : 0.01);
    spin->setAccelerated(true);
    spin->setSuffix(suffix);
    spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spin->setMinimumWidth(92);

    layout->addWidget(name, row, 0);
    layout->addWidget(slider, row, 1);
    layout->addWidget(spin, row, 2);
    return {slider, spin};
}

void SetChannelValue(const ChannelRow &row, double value,
                     double minValue, double maxValue)
{
    const QSignalBlocker blockSlider(row.slider);
    const QSignalBlocker blockSpin(row.spin);
    row.spin->setValue(value);
    const double t = (maxValue > minValue)
                         ? (value - minValue) / (maxValue - minValue)
                         : 0.0;
    row.slider->setValue(static_cast<int>(std::clamp(t, 0.0, 1.0) * 1000.0 +
                                          0.5));
}

void ConnectChannel(const ChannelRow &row, double minValue, double maxValue,
                    const std::function<void(double)> &changed)
{
    QObject::connect(row.slider, &QSlider::valueChanged, row.spin,
                     [row, minValue, maxValue, changed](int sliderValue) {
                         const double t = static_cast<double>(sliderValue) / 1000.0;
                         const double value = minValue + (maxValue - minValue) * t;
                         {
                             const QSignalBlocker block(row.spin);
                             row.spin->setValue(value);
                         }
                         changed(value);
                     });
    QObject::connect(row.spin,
                     static_cast<void (QDoubleSpinBox::*)(double)>(
                         &QDoubleSpinBox::valueChanged),
                     row.slider,
                     [row, minValue, maxValue, changed](double value) {
                         const double t = (value - minValue) / (maxValue - minValue);
                         {
                             const QSignalBlocker block(row.slider);
                             row.slider->setValue(static_cast<int>(
                                 std::clamp(t, 0.0, 1.0) * 1000.0 + 0.5));
                         }
                         changed(value);
                     });
}

} // namespace

QColor ArchColorDialog::getColor(const QColor &initial,
                                 QWidget *parent,
                                 const QString &title,
                                 const std::function<void(const QColor &)> &previewChanged)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setMinimumWidth(650);

    QColor current = initial.isValid() ? initial : QColor(Qt::white);
    double hue = NormalizedHue(current, 0.0);
    float sat = 0.0f;
    float val = 0.0f;
    float initialHue = 0.0f;
    current.getHsvF(&initialHue, &sat, &val);
    bool syncing = false;

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    auto *top = new QHBoxLayout();
    top->setSpacing(12);

    auto *plane = new ColorPlane(&dialog);
    auto *hueStrip = new HueStrip(&dialog);
    top->addWidget(plane, 1);
    top->addWidget(hueStrip);

    auto *controls = new QGridLayout();
    controls->setHorizontalSpacing(8);
    controls->setVerticalSpacing(6);
    top->addLayout(controls, 1);

    ChannelRow rRow = CreateChannelRow(controls, 0, QObject::tr("R"), 0.0, 1.0, 3);
    ChannelRow gRow = CreateChannelRow(controls, 1, QObject::tr("G"), 0.0, 1.0, 3);
    ChannelRow bRow = CreateChannelRow(controls, 2, QObject::tr("B"), 0.0, 1.0, 3);
    ChannelRow hRow = CreateChannelRow(controls, 4, QObject::tr("H"), 0.0, 1.0, 3);
    ChannelRow sRow = CreateChannelRow(controls, 5, QObject::tr("S"), 0.0, 1.0, 3);
    ChannelRow vRow = CreateChannelRow(controls, 6, QObject::tr("V"), 0.0, 1.0, 3);
    ChannelRow kRow = CreateChannelRow(controls, 8, QObject::tr("K"), 1000.0, 40000.0, 0,
                                       QObject::tr(" K"));
    controls->setRowMinimumHeight(3, 8);
    controls->setRowMinimumHeight(7, 8);

    layout->addLayout(top, 1);

    auto *swatchRow = new QHBoxLayout();
    swatchRow->setSpacing(8);
    swatchRow->addWidget(new QLabel(QObject::tr("Previous"), &dialog));
    auto *previousSwatch = new Swatch(&dialog);
    previousSwatch->setColor(current);
    swatchRow->addWidget(previousSwatch);
    swatchRow->addWidget(new QLabel(QObject::tr("Current"), &dialog));
    auto *currentSwatch = new Swatch(&dialog);
    swatchRow->addWidget(currentSwatch);
    swatchRow->addStretch(1);
    layout->addLayout(swatchRow);

    auto *variationGrid = new QGridLayout();
    variationGrid->setHorizontalSpacing(8);
    variationGrid->setVerticalSpacing(4);
    auto *hueStripVariants = new VariationStrip(VariationStrip::Mode::Hue, &dialog);
    auto *satStripVariants =
        new VariationStrip(VariationStrip::Mode::Saturation, &dialog);
    auto *valStripVariants =
        new VariationStrip(VariationStrip::Mode::Value, &dialog);
    variationGrid->addWidget(new QLabel(QObject::tr("Hue Variation"), &dialog),
                             0, 0);
    variationGrid->addWidget(hueStripVariants, 0, 1);
    variationGrid->addWidget(new QLabel(QObject::tr("Sat Variation"), &dialog),
                             1, 0);
    variationGrid->addWidget(satStripVariants, 1, 1);
    variationGrid->addWidget(new QLabel(QObject::tr("Val Variation"), &dialog),
                             2, 0);
    variationGrid->addWidget(valStripVariants, 2, 1);
    variationGrid->setColumnStretch(1, 1);
    layout->addLayout(variationGrid);

    auto refreshUi = [&]() {
        syncing = true;
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        current.getRgbF(&r, &g, &b);
        float hsvHue = 0.0f;
        current.getHsvF(&hsvHue, &sat, &val);
        hue = NormalizedHue(current, hue);

        SetChannelValue(rRow, r, 0.0, 1.0);
        SetChannelValue(gRow, g, 0.0, 1.0);
        SetChannelValue(bRow, b, 0.0, 1.0);
        SetChannelValue(hRow, hue, 0.0, 1.0);
        SetChannelValue(sRow, sat, 0.0, 1.0);
        SetChannelValue(vRow, val, 0.0, 1.0);

        plane->setValues(hue, sat, val);
        hueStrip->setHue(hue);
        currentSwatch->setColor(current);
        hueStripVariants->setBaseColor(current, hue);
        satStripVariants->setBaseColor(current, hue);
        valStripVariants->setBaseColor(current, hue);
        syncing = false;
    };

    auto setColor = [&](const QColor &color) {
        if (syncing || !color.isValid()) {
            return;
        }
        current = color;
        refreshUi();
        if (previewChanged) {
            previewChanged(current);
        }
    };

    auto setRgb = [&](double r, double g, double b) {
        setColor(QColor::fromRgbF(std::clamp(r, 0.0, 1.0),
                                  std::clamp(g, 0.0, 1.0),
                                  std::clamp(b, 0.0, 1.0)));
    };

    ConnectChannel(rRow, 0.0, 1.0, [&](double value) {
        if (!syncing) setRgb(value, current.greenF(), current.blueF());
    });
    ConnectChannel(gRow, 0.0, 1.0, [&](double value) {
        if (!syncing) setRgb(current.redF(), value, current.blueF());
    });
    ConnectChannel(bRow, 0.0, 1.0, [&](double value) {
        if (!syncing) setRgb(current.redF(), current.greenF(), value);
    });
    ConnectChannel(hRow, 0.0, 1.0, [&](double value) {
        if (!syncing) {
            hue = value;
            setColor(QColor::fromHsvF(hue, sat, val));
        }
    });
    ConnectChannel(sRow, 0.0, 1.0, [&](double value) {
        if (!syncing) setColor(QColor::fromHsvF(hue, value, val));
    });
    ConnectChannel(vRow, 0.0, 1.0, [&](double value) {
        if (!syncing) setColor(QColor::fromHsvF(hue, sat, value));
    });
    ConnectChannel(kRow, 1000.0, 40000.0, [&](double value) {
        if (!syncing) setColor(colorForKelvin(value));
    });

    plane->setChangedCallback([&](double saturation, double value) {
        if (!syncing) setColor(QColor::fromHsvF(hue, saturation, value));
    });
    hueStrip->setChangedCallback([&](double value) {
        if (!syncing) {
            hue = value;
            setColor(QColor::fromHsvF(hue, sat, val));
        }
    });
    hueStripVariants->setChangedCallback([&](const QColor &color) {
        if (!syncing) setColor(color);
    });
    satStripVariants->setChangedCallback([&](const QColor &color) {
        if (!syncing) setColor(color);
    });
    valStripVariants->setChangedCallback([&](const QColor &color) {
        if (!syncing) setColor(color);
    });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok |
                                         QDialogButtonBox::Cancel,
                                         &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted,
                     &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected,
                     &dialog, &QDialog::reject);

    SetChannelValue(kRow, 6500.0, 1000.0, 40000.0);
    refreshUi();

    if (dialog.exec() != QDialog::Accepted) {
        return QColor();
    }
    return current;
}

void ArchColorDialog::showColor(
    const QColor &initial,
    QWidget *parent,
    const QString &title,
    const std::function<void(const QColor &)> &previewChanged,
    const std::function<void(const QColor &)> &accepted,
    const std::function<void()> &rejected)
{
    auto *dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(title);
    dialog->setMinimumWidth(650);

    struct State {
        QColor current;
        double hue = 0.0;
        float sat = 0.0f;
        float val = 0.0f;
        bool syncing = false;
    };
    auto state = std::make_shared<State>();
    state->current = initial.isValid() ? initial : QColor(Qt::white);
    state->hue = NormalizedHue(state->current, 0.0);
    float initialHue = 0.0f;
    state->current.getHsvF(&initialHue, &state->sat, &state->val);

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    auto *top = new QHBoxLayout();
    top->setSpacing(12);

    auto *plane = new ColorPlane(dialog);
    auto *hueStrip = new HueStrip(dialog);
    top->addWidget(plane, 1);
    top->addWidget(hueStrip);

    auto *controls = new QGridLayout();
    controls->setHorizontalSpacing(8);
    controls->setVerticalSpacing(6);
    top->addLayout(controls, 1);

    ChannelRow rRow = CreateChannelRow(controls, 0, QObject::tr("R"), 0.0, 1.0, 3);
    ChannelRow gRow = CreateChannelRow(controls, 1, QObject::tr("G"), 0.0, 1.0, 3);
    ChannelRow bRow = CreateChannelRow(controls, 2, QObject::tr("B"), 0.0, 1.0, 3);
    ChannelRow hRow = CreateChannelRow(controls, 4, QObject::tr("H"), 0.0, 1.0, 3);
    ChannelRow sRow = CreateChannelRow(controls, 5, QObject::tr("S"), 0.0, 1.0, 3);
    ChannelRow vRow = CreateChannelRow(controls, 6, QObject::tr("V"), 0.0, 1.0, 3);
    ChannelRow kRow = CreateChannelRow(controls, 8, QObject::tr("K"), 1000.0, 40000.0, 0,
                                       QObject::tr(" K"));
    controls->setRowMinimumHeight(3, 8);
    controls->setRowMinimumHeight(7, 8);

    layout->addLayout(top, 1);

    auto *swatchRow = new QHBoxLayout();
    swatchRow->setSpacing(8);
    swatchRow->addWidget(new QLabel(QObject::tr("Previous"), dialog));
    auto *previousSwatch = new Swatch(dialog);
    previousSwatch->setColor(state->current);
    swatchRow->addWidget(previousSwatch);
    swatchRow->addWidget(new QLabel(QObject::tr("Current"), dialog));
    auto *currentSwatch = new Swatch(dialog);
    swatchRow->addWidget(currentSwatch);
    swatchRow->addStretch(1);
    layout->addLayout(swatchRow);

    auto *variationGrid = new QGridLayout();
    variationGrid->setHorizontalSpacing(8);
    variationGrid->setVerticalSpacing(4);
    auto *hueStripVariants = new VariationStrip(VariationStrip::Mode::Hue, dialog);
    auto *satStripVariants =
        new VariationStrip(VariationStrip::Mode::Saturation, dialog);
    auto *valStripVariants =
        new VariationStrip(VariationStrip::Mode::Value, dialog);
    variationGrid->addWidget(new QLabel(QObject::tr("Hue Variation"), dialog),
                             0, 0);
    variationGrid->addWidget(hueStripVariants, 0, 1);
    variationGrid->addWidget(new QLabel(QObject::tr("Sat Variation"), dialog),
                             1, 0);
    variationGrid->addWidget(satStripVariants, 1, 1);
    variationGrid->addWidget(new QLabel(QObject::tr("Val Variation"), dialog),
                             2, 0);
    variationGrid->addWidget(valStripVariants, 2, 1);
    variationGrid->setColumnStretch(1, 1);
    layout->addLayout(variationGrid);

    std::shared_ptr<std::function<void()>> refreshUi =
        std::make_shared<std::function<void()>>();
    std::shared_ptr<std::function<void(const QColor &)>> setColor =
        std::make_shared<std::function<void(const QColor &)>>();

    *refreshUi = [=]() {
        state->syncing = true;
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        state->current.getRgbF(&r, &g, &b);
        float hsvHue = 0.0f;
        state->current.getHsvF(&hsvHue, &state->sat, &state->val);
        state->hue = NormalizedHue(state->current, state->hue);

        SetChannelValue(rRow, r, 0.0, 1.0);
        SetChannelValue(gRow, g, 0.0, 1.0);
        SetChannelValue(bRow, b, 0.0, 1.0);
        SetChannelValue(hRow, state->hue, 0.0, 1.0);
        SetChannelValue(sRow, state->sat, 0.0, 1.0);
        SetChannelValue(vRow, state->val, 0.0, 1.0);

        plane->setValues(state->hue, state->sat, state->val);
        hueStrip->setHue(state->hue);
        currentSwatch->setColor(state->current);
        hueStripVariants->setBaseColor(state->current, state->hue);
        satStripVariants->setBaseColor(state->current, state->hue);
        valStripVariants->setBaseColor(state->current, state->hue);
        state->syncing = false;
    };

    *setColor = [=](const QColor &color) {
        if (state->syncing || !color.isValid()) {
            return;
        }
        state->current = color;
        (*refreshUi)();
        if (previewChanged) {
            previewChanged(state->current);
        }
    };

    auto setRgb = [=](double r, double g, double b) {
        (*setColor)(QColor::fromRgbF(std::clamp(r, 0.0, 1.0),
                                     std::clamp(g, 0.0, 1.0),
                                     std::clamp(b, 0.0, 1.0)));
    };

    ConnectChannel(rRow, 0.0, 1.0, [=](double value) {
        if (!state->syncing) setRgb(value, state->current.greenF(), state->current.blueF());
    });
    ConnectChannel(gRow, 0.0, 1.0, [=](double value) {
        if (!state->syncing) setRgb(state->current.redF(), value, state->current.blueF());
    });
    ConnectChannel(bRow, 0.0, 1.0, [=](double value) {
        if (!state->syncing) setRgb(state->current.redF(), state->current.greenF(), value);
    });
    ConnectChannel(hRow, 0.0, 1.0, [=](double value) {
        if (!state->syncing) {
            state->hue = value;
            (*setColor)(QColor::fromHsvF(state->hue, state->sat, state->val));
        }
    });
    ConnectChannel(sRow, 0.0, 1.0, [=](double value) {
        if (!state->syncing) (*setColor)(QColor::fromHsvF(state->hue, value, state->val));
    });
    ConnectChannel(vRow, 0.0, 1.0, [=](double value) {
        if (!state->syncing) (*setColor)(QColor::fromHsvF(state->hue, state->sat, value));
    });
    ConnectChannel(kRow, 1000.0, 40000.0, [=](double value) {
        if (!state->syncing) (*setColor)(colorForKelvin(value));
    });

    plane->setChangedCallback([=](double saturation, double value) {
        if (!state->syncing) (*setColor)(QColor::fromHsvF(state->hue, saturation, value));
    });
    hueStrip->setChangedCallback([=](double value) {
        if (!state->syncing) {
            state->hue = value;
            (*setColor)(QColor::fromHsvF(state->hue, state->sat, state->val));
        }
    });
    hueStripVariants->setChangedCallback([=](const QColor &color) {
        if (!state->syncing) (*setColor)(color);
    });
    satStripVariants->setChangedCallback([=](const QColor &color) {
        if (!state->syncing) (*setColor)(color);
    });
    valStripVariants->setChangedCallback([=](const QColor &color) {
        if (!state->syncing) (*setColor)(color);
    });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok |
                                         QDialogButtonBox::Cancel,
                                         dialog);
    layout->addWidget(buttons);
    auto completed = std::make_shared<bool>(false);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, [=]() {
        *completed = true;
        if (accepted) {
            accepted(state->current);
        }
        dialog->accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, [=]() {
        *completed = true;
        if (rejected) {
            rejected();
        }
        dialog->reject();
    });
    QObject::connect(dialog, &QDialog::finished, dialog, [=](int result) {
        if (!*completed && result != QDialog::Accepted) {
            *completed = true;
            if (rejected) {
                rejected();
            }
        }
    });

    SetChannelValue(kRow, 6500.0, 1000.0, 40000.0);
    (*refreshUi)();
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

QColor ArchColorDialog::colorForKelvin(double kelvin)
{
    const double temperature = std::clamp(kelvin, 1000.0, 40000.0) / 100.0;
    double r = 255.0;
    double g = 255.0;
    double b = 255.0;

    if (temperature <= 66.0) {
        r = 255.0;
        g = 99.4708025861 * std::log(temperature) - 161.1195681661;
        b = (temperature <= 19.0)
                ? 0.0
                : 138.5177312231 * std::log(temperature - 10.0) -
                      305.0447927307;
    } else {
        r = 329.698727446 * std::pow(temperature - 60.0, -0.1332047592);
        g = 288.1221695283 * std::pow(temperature - 60.0, -0.0755148492);
        b = 255.0;
    }

    return QColor::fromRgbF(ClampChannel(r),
                            ClampChannel(g),
                            ClampChannel(b),
                            1.0);
}
