/*
* All or portions of this file Copyright (c) Amazon.com, Inc. or its affiliates or
* its licensors.
*
* For complete copyright and license terms please see the LICENSE at the root of this
* distribution (the "License"). All use of this software is governed by the License,
* or, if provided, by the license below or the license accompanying this file. Do not
* remove or modify any license notices. This file is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*
*/
#include "precompiled.h"

#include <functional>

#include <QEvent>
#include <QGraphicsItem>
#include <QGraphicsGridLayout>
#include <QGraphicsLayoutItem>
#include <QGraphicsScene>
#include <qgraphicssceneevent.h>
#include <QGraphicsWidget>
#include <QPainter>

#include <Components/Nodes/Comment/CommentTextGraphicsWidget.h>

#include <GraphCanvas/Components/GeometryBus.h>
#include <GraphCanvas/Components/Nodes/NodeBus.h>
#include <GraphCanvas/Components/Nodes/NodeUIBus.h>
#include <GraphCanvas/Components/Slots/SlotBus.h>
#include <GraphCanvas/Components/VisualBus.h>
#include <GraphCanvas/tools.h>

namespace GraphCanvas
{
    //////////////////////////////////
    // CommentNodeTextGraphicsWidget
    //////////////////////////////////
    CommentTextGraphicsWidget::CommentTextGraphicsWidget(const AZ::EntityId& targetId)
        : m_commentMode(CommentMode::Unknown)
        , m_editable(false)
        , m_layoutLock(false)
        , m_pressed(false)
        , m_entityId(targetId)
    {
        setFlag(ItemIsMovable, false);

        m_displayLabel = aznew GraphCanvasLabel();

        m_proxyWidget = new QGraphicsProxyWidget();
        m_proxyWidget->setFocusPolicy(Qt::FocusPolicy::StrongFocus);

        m_timer.setSingleShot(true);
        m_timer.setInterval(100);
        m_timer.stop();

        QObject::connect(&m_timer, &QTimer::timeout, [&]()
            {
                m_textEdit->setFocus(Qt::FocusReason::MouseFocusReason);
                m_proxyWidget->setFocus(Qt::FocusReason::MouseFocusReason);
            });

        m_textEdit = aznew Internal::FocusableTextEdit();
        m_textEdit->setProperty("HasNoWindowDecorations", true);
        m_textEdit->setProperty("DisableFocusWindowFix", true);
        m_textEdit->setFocusPolicy(Qt::FocusPolicy::StrongFocus);

        m_textEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarPolicy::ScrollBarAlwaysOff);
        m_textEdit->setVerticalScrollBarPolicy(Qt::ScrollBarPolicy::ScrollBarAlwaysOff);
        m_textEdit->setSizeAdjustPolicy(QAbstractScrollArea::SizeAdjustPolicy::AdjustToContents);

        m_textEdit->setEnabled(true);

        QObject::connect(m_textEdit, &Internal::FocusableTextEdit::textChanged, [this]() { this->UpdateSizing(); });
        QObject::connect(m_textEdit, &Internal::FocusableTextEdit::OnFocusIn,   [this]() { this->m_layoutLock = true; });
        QObject::connect(m_textEdit, &Internal::FocusableTextEdit::OnFocusOut, [this]() { this->SubmitValue(); this->m_layoutLock = false; this->SetEditable(false); });
        m_proxyWidget->setWidget(m_textEdit);

        m_layout = new QGraphicsLinearLayout(Qt::Vertical);
        m_layout->setSpacing(0);
        m_layout->setContentsMargins(0, 0, 0, 0);
        m_layout->setInstantInvalidatePropagation(true);

        m_layout->addItem(m_displayLabel);

        setLayout(m_layout);
        setData(GraphicsItemName, QStringLiteral("Comment/%1").arg(static_cast<AZ::u64>(GetEntityId()), 16, 16, QChar('0')));

        SetCommentMode(CommentMode::Comment);
    }

    void CommentTextGraphicsWidget::Activate()
    {
        CommentUIRequestBus::Handler::BusConnect(GetEntityId());
        CommentLayoutRequestBus::Handler::BusConnect(GetEntityId());
        StyleNotificationBus::Handler::BusConnect(GetEntityId());

        UpdateLayout();
    }

    void CommentTextGraphicsWidget::Deactivate()
    {
        StyleNotificationBus::Handler::BusDisconnect();
        CommentLayoutRequestBus::Handler::BusDisconnect();
        CommentUIRequestBus::Handler::BusDisconnect();
    }

    void CommentTextGraphicsWidget::OnAddedToScene()
    {
        UpdateSizing();
    }

    void CommentTextGraphicsWidget::SetStyle(const AZStd::string& style)
    {
        m_style = style;
        OnStyleChanged();
    }

    void CommentTextGraphicsWidget::UpdateLayout()
    {
        if (m_layoutLock)
        {
            return;
        }

        QGraphicsScene* graphicsScene = nullptr;
        AZ::EntityId sceneId;
        SceneMemberRequestBus::EventResult(sceneId, GetEntityId(), &SceneMemberRequests::GetScene);

        SceneRequestBus::EventResult(graphicsScene, sceneId, &SceneRequests::AsQGraphicsScene);

        prepareGeometryChange();

        for (int i = m_layout->count() - 1; i >= 0; --i)
        {
            QGraphicsLayoutItem* layoutItem = m_layout->itemAt(i);
            m_layout->removeAt(i);
            layoutItem->setParentLayoutItem(nullptr);

            if (graphicsScene)
            {
                graphicsScene->removeItem(layoutItem->graphicsItem());
            }
        }

        if (m_editable)
        {
            // Adjust the size of the editable widget to match the label
            UpdateSizing();

            m_layout->addItem(m_proxyWidget);
        }
        else
        {
            m_layout->addItem(m_displayLabel);
        }

        updateGeometry();
        RefreshDisplay();

        m_layout->invalidate();
    }

    void CommentTextGraphicsWidget::UpdateStyles()
    {
        Styling::StyleHelper overallStyle(GetEntityId());
        qreal margin = overallStyle.GetAttribute(Styling::Attribute::Margin, 0.0);

        m_layout->setContentsMargins(margin, margin, margin, margin);

        m_displayLabel->SetStyle(GetEntityId(), m_style.c_str());

        const Styling::StyleHelper& styleHelper = m_displayLabel->GetStyleHelper();

        QPen border = styleHelper.GetBorder();

        // We construct a stylesheet for the Qt widget based on our calculated style
        QStringList fields;

        fields.push_back("background-color: rgba(0,0,0,0)");

        fields.push_back(QString("border-width: %1").arg(border.width()));

        switch (border.style())
        {
        case Qt::PenStyle::SolidLine:
            fields.push_back("border-style: solid");
            break;
        case Qt::PenStyle::DashLine:
            fields.push_back("border-style: dashed");
            break;
        case Qt::PenStyle::DotLine:
            fields.push_back("border-style: dotted");
            break;
        default:
            fields.push_back("border-style: none");
        }

        fields.push_back(QString("border-color: rgba(%1,%2,%3,%4)").arg(border.color().red()).arg(border.color().green()).arg(border.color().blue()).arg(border.color().alpha()));
        fields.push_back(QString("border-radius: %1").arg(styleHelper.GetAttribute(Styling::Attribute::BorderRadius, 0)));

        fields.push_back("margin: 0");
        fields.push_back("padding: 0");

        fields.push_back(styleHelper.GetFontStyleSheet());

        m_textEdit->setStyleSheet(fields.join("; ").toUtf8().data());

        if (styleHelper.HasTextAlignment())
        {
            m_textEdit->setAlignment(styleHelper.GetTextAlignment(m_textEdit->alignment()));
        }

        UpdateSizing();
    }

    void CommentTextGraphicsWidget::RefreshDisplay()
    {
        updateGeometry();
        update();
    }

    void CommentTextGraphicsWidget::SetComment(const AZStd::string& comment)
    {
        if (!comment.empty())
        {
            m_displayLabel->SetLabel(comment);
            m_textEdit->setPlainText(comment.c_str());
        }
        else
        {
            // Hack to force the minimum height based on the style's font/size
            m_displayLabel->SetLabel(" ");
            m_textEdit->setPlainText(" ");
        }

        UpdateSizing();
    }

    AZStd::string CommentTextGraphicsWidget::GetComment() const
    {
        return m_textEdit->toPlainText().toUtf8().data();
    }

    Styling::StyleHelper& CommentTextGraphicsWidget::GetStyleHelper()
    {
        return m_displayLabel->GetStyleHelper();
    }

    const Styling::StyleHelper& CommentTextGraphicsWidget::GetStyleHelper() const
    {
        return m_displayLabel->GetStyleHelper();
    }

    void CommentTextGraphicsWidget::SetCommentMode(CommentMode commentMode)
    {
        if (m_commentMode != commentMode)
        {
            m_commentMode = commentMode;

            prepareGeometryChange();

            switch (m_commentMode)
            {
            case CommentMode::BlockComment:
                setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
                m_layout->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
                m_textEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
                m_displayLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
                m_proxyWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

                m_textEdit->setWordWrapMode(QTextOption::WrapMode::NoWrap);

                m_displayLabel->SetElide(true);
                m_displayLabel->SetWrap(false);
                m_displayLabel->SetWrapMode(GraphCanvasLabel::WrapMode::BoundingWidth);
                break;
            case CommentMode::Comment:
                setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
                m_layout->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
                m_textEdit->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
                m_displayLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
                m_proxyWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

                m_textEdit->setWordWrapMode(QTextOption::WrapMode::WordWrap);

                m_displayLabel->SetElide(false);
                m_displayLabel->SetWrap(true);
                m_displayLabel->SetWrapMode(GraphCanvasLabel::WrapMode::MaximumWidth);
                break;
            default:
                AZ_Warning("Graph Canvas", false, "Unhandled Comment Mode: %i", m_commentMode);
            }

            updateGeometry();
        }
    }

    CommentMode CommentTextGraphicsWidget::GetCommentMode() const
    {
        return m_commentMode;
    }

    void CommentTextGraphicsWidget::SetEditable(bool editable)
    {
        if (m_editable != editable)
        {
            m_editable = editable;
            UpdateLayout();

            if (m_editable)
            {
                AZ::EntityId sceneId;
                SceneMemberRequestBus::EventResult(sceneId, GetEntityId(), &SceneMemberRequests::GetScene);
                SceneNotificationBus::Event(sceneId, &SceneNotifications::OnNodeIsBeingEdited, true);

                CommentNotificationBus::Event(GetEntityId(), &CommentNotifications::OnEditBegin);
                UpdateSizing();

                StyledEntityRequestBus::Event(GetEntityId(), &StyledEntityRequests::AddSelectorState, Styling::States::Editing);
                m_textEdit->selectAll();

                SceneMemberUIRequestBus::Event(GetEntityId(), &SceneMemberUIRequests::SetSelected, true);

                // Set the focus after a short delay to give Qt time to update
                m_timer.start();
            }
            else
            {
                AZ::EntityId sceneId;
                SceneMemberRequestBus::EventResult(sceneId, GetEntityId(), &SceneMemberRequests::GetScene);
                SceneNotificationBus::Event(sceneId, &SceneNotifications::OnNodeIsBeingEdited, false);

                CommentNotificationBus::Event(GetEntityId(), &CommentNotifications::OnEditEnd);
                StyledEntityRequestBus::Event(GetEntityId(), &StyledEntityRequests::RemoveSelectorState, Styling::States::Editing);
                m_layoutLock = false;
            }

            OnStyleChanged();
        }
    }

    QGraphicsLayoutItem* CommentTextGraphicsWidget::GetGraphicsLayoutItem()
    {
        return this;
    }

    void CommentTextGraphicsWidget::OnStyleChanged()
    {
        UpdateStyles();
        RefreshDisplay();
    }

    void CommentTextGraphicsWidget::UpdateSizing()
    {
        AZStd::string value = GetComment();
        m_displayLabel->SetLabel(value);

        QSizeF oldSize = m_textEdit->minimumSize();

        prepareGeometryChange();

        // As we update the label with the new contents, adjust the editable widget size to match
        if (m_commentMode == CommentMode::Comment)
        {
            m_textEdit->setMinimumSize(m_displayLabel->preferredSize().toSize());
            m_textEdit->setMaximumSize(m_displayLabel->preferredSize().toSize());
        }
        else if (m_commentMode == CommentMode::BlockComment)
        {
            QSizeF preferredSize = m_displayLabel->preferredSize();
            QRectF displaySize = m_displayLabel->GetDisplayedSize();

            displaySize.setHeight(preferredSize.height());

            if (displaySize.width() == 0 || displaySize.height() == 0)
            {
                return;
            }

            m_textEdit->setMinimumSize(displaySize.size().toSize());
            m_textEdit->setMaximumSize(displaySize.size().toSize());
        }

        QSizeF newSize = m_textEdit->minimumSize();

        if (oldSize != newSize)
        {
            CommentNotificationBus::Event(GetEntityId(), &CommentNotifications::OnCommentSizeChanged, oldSize, newSize);
        }

        updateGeometry();
    }

    void CommentTextGraphicsWidget::SubmitValue()
    {
        AZStd::string comment = GetComment();
        CommentRequestBus::Event(GetEntityId(), &CommentRequests::SetComment, comment);
        CommentNotificationBus::Event(GetEntityId(), &CommentNotifications::OnCommentChanged, comment);
        UpdateSizing();
    }

    bool CommentTextGraphicsWidget::sceneEventFilter(QGraphicsItem*, QEvent* event)
    {
        bool consumeEvent = event->isAccepted();

        switch (event->type())
        {
        case QEvent::GraphicsSceneMouseDoubleClick:
            consumeEvent = true;
            // Need to queue this since if we change out display in the middle of processing input
            // Things get sad.
            QTimer::singleShot(0, [this]() { this->SetEditable(true); });
            break;
        }

        return consumeEvent;
    }
#include <Source/Components/Nodes/Comment/CommentTextGraphicsWidget.moc>
}