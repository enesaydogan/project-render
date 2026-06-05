#pragma once

#include <QAbstractSpinBox>
#include <QApplication>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QWidget>

#include <algorithm>
#include <cmath>

class SliderControl : public QWidget
{
public:
    explicit SliderControl(double minValue,
                           double maxValue,
                           double step,
                           int decimals,
                           QWidget *parent = nullptr)
        : QWidget(parent)
        , m_minValue(minValue)
        , m_maxValue(std::max(maxValue, minValue))
        , m_slider(new QSlider(Qt::Horizontal, this))
        , m_spinBox(new QDoubleSpinBox(this))
        , m_layout(new QGridLayout(this))
    {
        m_layout->setContentsMargins(0, 0, 0, 0);
        m_layout->setHorizontalSpacing(8);
        m_layout->setVerticalSpacing(5);

        m_slider->setRange(0, kSliderSteps);
        m_slider->setSingleStep(1);
        m_slider->setPageStep(kSliderSteps / 20);

        m_spinBox->setRange(m_minValue, m_maxValue);
        m_spinBox->setSingleStep(step);
        m_spinBox->setDecimals(decimals);
        m_spinBox->setAccelerated(true);
        m_spinBox->setButtonSymbols(QAbstractSpinBox::NoButtons);
        m_spinBox->setMinimumWidth(60);
        m_spinBox->setMaximumWidth(80);
        m_slider->setMinimumWidth(100);

        m_layout->addWidget(m_slider, 0, 0);
        m_layout->addWidget(m_spinBox, 0, 1);
        m_layout->setColumnStretch(0, 1);

        connect(m_slider, &QSlider::valueChanged, this, [this](int sliderValue) {
            if (m_updating) {
                return;
            }
            m_updating = true;
            m_spinBox->setValue(sliderToValue(sliderValue));
            m_updating = false;
        });

        connect(m_spinBox,
                qOverload<double>(&QDoubleSpinBox::valueChanged),
                this,
                [this](double spinValue) {
                    if (m_updating) {
                        return;
                    }
                    m_updating = true;
                    m_slider->setValue(valueToSlider(spinValue));
                    m_updating = false;
                });

        setValue(m_minValue);
    }

    void setValue(double value)
    {
        const QSignalBlocker sliderBlocker(m_slider);
        const QSignalBlocker spinBlocker(m_spinBox);
        const double clamped = std::clamp(value, m_minValue, m_maxValue);
        m_spinBox->setValue(clamped);
        m_slider->setValue(valueToSlider(clamped));
    }

    double value() const
    {
        return m_spinBox->value();
    }

    bool isInteracting() const
    {
        QWidget *focus = QApplication::focusWidget();
        return m_slider->isSliderDown() ||
               (focus && (focus == m_slider || focus == m_spinBox ||
                          m_slider->isAncestorOf(focus) ||
                          m_spinBox->isAncestorOf(focus)));
    }

    void setLogarithmic(bool enabled)
    {
        m_logarithmic = enabled && m_minValue > 0.0 && m_maxValue > m_minValue;
        setValue(m_spinBox->value());
    }

    void setSuffix(const QString &suffix)
    {
        m_spinBox->setSuffix(suffix);
    }

    void setStackedLabel(const QString &text)
    {
        if (!m_captionLabel) {
            m_captionLabel = new QLabel(this);
            m_captionLabel->setObjectName(QStringLiteral("SliderControlCaption"));
        }
        m_spinBox->setObjectName(QStringLiteral("SliderControlValue"));
        m_captionLabel->setText(text);
        m_layout->removeWidget(m_slider);
        m_layout->removeWidget(m_spinBox);
        m_layout->removeWidget(m_captionLabel);
        m_layout->addWidget(m_captionLabel, 0, 0);
        m_layout->addWidget(m_spinBox, 0, 1, Qt::AlignRight);
        m_layout->addWidget(m_slider, 1, 0, 1, 2);
        m_layout->setColumnStretch(0, 1);
        m_layout->setColumnStretch(1, 0);
        m_slider->setMinimumWidth(0);
        m_spinBox->setMinimumWidth(64);
        m_spinBox->setMaximumWidth(82);
    }

    QLabel *captionLabel() const
    {
        return m_captionLabel;
    }

    void setEnabled(bool enabled)
    {
        QWidget::setEnabled(enabled);
        m_slider->setEnabled(enabled);
        m_spinBox->setEnabled(enabled);
    }

    QDoubleSpinBox *spinBox() const
    {
        return m_spinBox;
    }

private:
    int valueToSlider(double value) const
    {
        const double normalized = valueToNormalized(value);
        return static_cast<int>(std::lround(normalized * kSliderSteps));
    }

    double sliderToValue(int sliderValue) const
    {
        const double normalized = static_cast<double>(sliderValue) /
                                  static_cast<double>(kSliderSteps);
        return normalizedToValue(normalized);
    }

    double valueToNormalized(double value) const
    {
        if (m_logarithmic) {
            const double minLog = std::log(m_minValue);
            const double maxLog = std::log(m_maxValue);
            return (std::log(std::clamp(value, m_minValue, m_maxValue)) - minLog) /
                   std::max(1.0e-9, maxLog - minLog);
        }
        return (std::clamp(value, m_minValue, m_maxValue) - m_minValue) /
               std::max(1.0e-9, m_maxValue - m_minValue);
    }

    double normalizedToValue(double normalized) const
    {
        normalized = std::clamp(normalized, 0.0, 1.0);
        if (m_logarithmic) {
            const double minLog = std::log(m_minValue);
            const double maxLog = std::log(m_maxValue);
            return std::exp(minLog + normalized * (maxLog - minLog));
        }
        return m_minValue + normalized * (m_maxValue - m_minValue);
    }

    static constexpr int kSliderSteps = 10000;

    double m_minValue = 0.0;
    double m_maxValue = 1.0;
    bool m_logarithmic = false;
    bool m_updating = false;
    QSlider *m_slider = nullptr;
    QDoubleSpinBox *m_spinBox = nullptr;
    QGridLayout *m_layout = nullptr;
    QLabel *m_captionLabel = nullptr;
};
