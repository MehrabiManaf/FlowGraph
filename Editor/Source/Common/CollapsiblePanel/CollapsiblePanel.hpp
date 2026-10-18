#pragma once

#include "PanelIcon.hpp"
#include <QPushButton>
#include <QLabel>
#include <QToolButton>
#include <QPointer>
#include <QPropertyAnimation>



class CCollapsiblePanel: public QWidget
{
    Q_OBJECT
    Q_PROPERTY(double AnimationProgress READ AnimationProgress WRITE SetAnimationProgress)
public:
    CCollapsiblePanel(const QString& Title, QWidget* parent = nullptr, bool HasPlusButton = true);
    ~CCollapsiblePanel();

public:
    void AddWidget(QWidget* Widget, bool MustExpandContents = false);
    void SetPlusButtonDisabled(const bool& Disabled);
    void SetContentsExpaned(bool Expanded);
    void UpdateContentsHeight();
    void SetTitle(const QString& Title) { mp_Label->setText(Title); }
    // Expands the panel at once (no animation): for panels that are created already open.
    void ExpandImmediately();
private:
    double AnimationProgress() const { return m_AnimationProgress; }
    void SetAnimationProgress(const double& Progress);
    int ComputedHeight() const;

    void resizeEvent(QResizeEvent* pEvent) override;
    QSize sizeHint() const override;
signals:
    void PlusButtonClicked();
private slots:
    void OnToggle(const bool& Expanded);
    void OnPlusButtonClicked(const bool&);


protected:
    // Button Members
    QPushButton*                                        mp_PanelButton;
    CPanelIcon*                                         mp_Icon;
    QLabel*                                             mp_Label;
    QToolButton*                                        mp_PlusButton;

    QWidget*                                            mp_Contents;
    QPropertyAnimation*                                 mp_ExpandCollapseAnimation;
    double                                              m_AnimationProgress;

};
