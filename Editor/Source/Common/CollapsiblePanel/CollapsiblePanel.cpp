#include "CollapsiblePanel.hpp"
#include <QVBoxLayout>
#include <QResizeEvent>
#include <QTimer>


CCollapsiblePanel::CCollapsiblePanel(const QString& Title, QWidget* parent, bool HasPlusButton):
    QWidget(parent)
{
    auto pMainLayout = new QVBoxLayout(this);
    pMainLayout->setAlignment(Qt::AlignTop);
    pMainLayout->setContentsMargins(0, 0, 0, 0);
    pMainLayout->setSpacing(0);

    // PanelButton
    {
        mp_PanelButton = new QPushButton(this);
        mp_PanelButton->setCheckable(true);
        mp_PanelButton->setChecked(false);
        mp_PanelButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        QHBoxLayout* pButtonLayout = new QHBoxLayout(mp_PanelButton);
        pButtonLayout->setContentsMargins(4, 0, 4, 0);
        pButtonLayout->setSpacing(2);

        mp_Icon = new CPanelIcon(mp_PanelButton);
        mp_Icon->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

        mp_Label = new QLabel(Title, mp_PanelButton);
        mp_Label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        mp_Label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

        mp_PlusButton = nullptr;
        if(HasPlusButton)
        {
            mp_PlusButton = new QToolButton(mp_PanelButton);
            mp_PlusButton->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
            mp_PlusButton->setFixedSize(16, 16);
            mp_PlusButton->setIcon(QIcon(":/Icons/add.svg"));
            mp_PlusButton->setIconSize(QSize(12, 12));
            mp_PlusButton->setText("+");                      // not shown (icon only), but names the button
            connect(mp_PlusButton, SIGNAL(clicked(bool)), this, SLOT(OnPlusButtonClicked(bool)));
        }

        pButtonLayout->addWidget(mp_Icon);
        pButtonLayout->addWidget(mp_Label);
        if(mp_PlusButton)
            pButtonLayout->addWidget(mp_PlusButton);

        mp_PanelButton->setLayout(pButtonLayout);

    }

    // Contents
    {
        mp_Contents = new QWidget(this);
        auto pContentsLayout = new QVBoxLayout(mp_Contents);
        pContentsLayout->setAlignment(Qt::AlignTop);
        pContentsLayout->setContentsMargins(6, 2, 6, 2);
        pContentsLayout->setSpacing(2);
        mp_Contents->setLayout(pContentsLayout);
        mp_Contents->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }

    mp_Contents->lower();
    mp_PanelButton->raise();

    pMainLayout->addWidget(mp_PanelButton);
    setLayout(pMainLayout);

    connect(mp_PanelButton, SIGNAL(toggled(bool)), this, SLOT(OnToggle(bool)));


    mp_ExpandCollapseAnimation = new QPropertyAnimation(this, "AnimationProgress", this);
    mp_ExpandCollapseAnimation->setStartValue(0.0);
    mp_ExpandCollapseAnimation->setEndValue(1.0);
    mp_ExpandCollapseAnimation->setEasingCurve(QEasingCurve::InOutQuad);
    mp_ExpandCollapseAnimation->setDuration(333);

    m_AnimationProgress = 0.0;
}

CCollapsiblePanel::~CCollapsiblePanel()
{

}


void CCollapsiblePanel::AddWidget(QWidget* Widget, bool MustExpandContents)
{
    mp_Contents->layout()->addWidget(Widget);
    mp_Icon->SetIconVisible(mp_Contents->layout()->count() > 0);
    if(MustExpandContents && !mp_PanelButton->isChecked())
        SetContentsExpaned(true);
    else if(mp_PanelButton->isChecked())
        UpdateContentsHeight();
}

void CCollapsiblePanel::ExpandImmediately()
{
    mp_PanelButton->blockSignals(true);
    mp_PanelButton->setChecked(true);
    mp_PanelButton->blockSignals(false);
    SetAnimationProgress(1.0);
}

void CCollapsiblePanel::SetPlusButtonDisabled(const bool& Disabled)
{
    if(mp_PlusButton)
        mp_PlusButton->setDisabled(Disabled);
}

void CCollapsiblePanel::SetContentsExpaned(bool Expanded)
{
    if(mp_PanelButton->isChecked() == Expanded)
        return;
    mp_PanelButton->setChecked(Expanded);
}

void CCollapsiblePanel::UpdateContentsHeight()
{
    if(!mp_PanelButton->isChecked())
        return;
    // todo: animate height from current to what it must be
    QTimer::singleShot(0, this, [this]()
    {
        SetAnimationProgress(1.0);
    });
}

void CCollapsiblePanel::SetAnimationProgress(const double& Progress)
{
    if((Progress < 0.0) || (Progress > 1.0) /*|| (Progress == m_AnimationProgress)*/)
        return;
    m_AnimationProgress = Progress;
    setFixedHeight(ComputedHeight());
    mp_Icon->RotateIcon(m_AnimationProgress * 90);
}

// The height the panel must have right now, between the collapsed and the fully expanded height according to
// m_AnimationProgress. sizeHint() uses the same formula: the enclosing layout must always agree with the actual,
// still-animating height, or the row below starts the move to its final position before this panel has shrunk (or
// grown) to match, and the two widgets overlap for the rest of the animation.
int CCollapsiblePanel::ComputedHeight() const
{
    int CollapsedHeight = mp_PanelButton->sizeHint().height();
    int ExpandedHeight = CollapsedHeight;
    if(mp_Contents->layout()->count() > 0)
        ExpandedHeight += mp_Contents->sizeHint().height();
    return CollapsedHeight + (m_AnimationProgress * (ExpandedHeight - CollapsedHeight));
}

void CCollapsiblePanel::resizeEvent(QResizeEvent* pEvent)
{
    auto Size = pEvent->size();
    QWidget::resizeEvent(pEvent);
    Size.setHeight(mp_Contents->sizeHint().height());
    auto DesiredPos = QPoint(0, mp_PanelButton->sizeHint().height());
    if(mp_Contents->pos() != DesiredPos)
        mp_Contents->move(DesiredPos);
    mp_Contents->resize(Size);
}

QSize CCollapsiblePanel::sizeHint() const
{
    QSize SizeHint = mp_PanelButton->sizeHint();
    SizeHint.setHeight(ComputedHeight());
    return SizeHint;
}

void CCollapsiblePanel::OnToggle(const bool& Expanded)
{
    mp_ExpandCollapseAnimation->setDirection(Expanded? QAbstractAnimation::Forward: QAbstractAnimation::Backward);
    mp_ExpandCollapseAnimation->start();
}

void CCollapsiblePanel::OnPlusButtonClicked(const bool&)
{
    emit PlusButtonClicked();
}
