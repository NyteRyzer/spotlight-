#include "ui/designstudio.h"

#include "core/project.h"
#include "media/mediaprobe.h"
#include "ui/designcanvas.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFormLayout>
#include <QImageReader>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace spotlight {

namespace {
QSlider *hslider(int min, int max, QWidget *parent)
{
    auto *s = new QSlider(Qt::Horizontal, parent);
    s->setRange(min, max);
    return s;
}

void setSwatch(QPushButton *b, const QColor &c)
{
    b->setStyleSheet(QStringLiteral("QPushButton { background: %1; border: 1px solid #555; min-width: 48px; }")
                         .arg(c.name(QColor::HexArgb)));
}
} // namespace

DesignStudio::DesignStudio(Project *project, QWidget *parent)
    : QMainWindow(parent)
    , m_project(project)
    , m_canvas(new DesignCanvas(project, this))
    , m_layers(new QListWidget(this))
    , m_preset(new QComboBox(this))
    , m_bgButton(new QPushButton(this))
{
    setObjectName(QStringLiteral("DesignStudio"));
    setWindowFlags(Qt::Widget);
    setDockNestingEnabled(true);
    setCentralWidget(m_canvas);

    // ---- Toolbar -------------------------------------------------------
    auto *tb = new QToolBar(tr("Design Tools"), this);
    tb->setObjectName(QStringLiteral("design.tools"));
    tb->setMovable(false);
    for (const auto &p : canvasPresets())
        m_preset->addItem(p.name, p.size);
    m_preset->addItem(tr("Custom…"));
    tb->addWidget(new QLabel(tr(" Canvas "), this));
    tb->addWidget(m_preset);
    m_bgButton->setToolTip(tr("Background color"));
    tb->addWidget(m_bgButton);
    tb->addSeparator();
    tb->addAction(tr("T  Text"), this, &DesignStudio::addText);
    tb->addAction(tr("Image…"), this, &DesignStudio::addImageDialog);
    auto *shapeBtn = new QToolButton(this);
    shapeBtn->setText(tr("Shape"));
    shapeBtn->setPopupMode(QToolButton::InstantPopup);
    auto *shapeMenu = new QMenu(shapeBtn);
    shapeMenu->addAction(tr("Rectangle"), this, [this] { addShape(ShapeKind::Rectangle); });
    shapeMenu->addAction(tr("Ellipse"), this, [this] { addShape(ShapeKind::Ellipse); });
    shapeBtn->setMenu(shapeMenu);
    tb->addWidget(shapeBtn);
    tb->addAction(tr("Use Video Frame"), this, &DesignStudio::requestVideoFrame)
        ->setToolTip(tr("Grab the current Program Monitor frame as a new layer"));
    tb->addSeparator();
    tb->addAction(QStringLiteral("▲"), this, [this] { moveSelected(1); })->setToolTip(tr("Bring forward"));
    tb->addAction(QStringLiteral("▼"), this, [this] { moveSelected(-1); })->setToolTip(tr("Send backward"));
    tb->addAction(tr("Delete"), this, &DesignStudio::deleteSelected);
    tb->addSeparator();
    auto *exportAct = tb->addAction(tr("Export Image…"), this, &DesignStudio::exportImage);
    exportAct->setToolTip(tr("Save the design as PNG or JPEG"));
    addToolBar(Qt::TopToolBarArea, tb);

    connect(m_preset, &QComboBox::activated, this, [this](int i) {
        QSize size = m_preset->itemData(i).toSize();
        if (!size.isValid()) {
            bool ok = false;
            const QSize cur = m_project->data().design.canvasSize;
            QDialog dlg(this);
            dlg.setWindowTitle(tr("Custom Canvas Size"));
            auto *form = new QFormLayout(&dlg);
            auto *w = new QSpinBox(&dlg);
            auto *h = new QSpinBox(&dlg);
            w->setRange(16, 8192);
            h->setRange(16, 8192);
            w->setValue(cur.width());
            h->setValue(cur.height());
            form->addRow(tr("Width"), w);
            form->addRow(tr("Height"), h);
            auto *okBtn = new QPushButton(tr("OK"), &dlg);
            form->addRow(okBtn);
            connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
            ok = dlg.exec() == QDialog::Accepted;
            if (!ok)
                return;
            size = QSize(w->value(), h->value());
        }
        m_project->edit(tr("Canvas Size"), [&](ProjectData &d) { d.design.canvasSize = size; });
    });
    connect(m_bgButton, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_project->data().design.background, this, tr("Background"));
        if (c.isValid())
            m_project->edit(tr("Background Color"), [&](ProjectData &d) { d.design.background = c; });
    });

    // ---- Layers dock ---------------------------------------------------
    m_layers->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);

    connect(m_layers, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *cur) {
        if (m_updating)
            return;
        m_canvas->setSelectedLayer(cur ? QUuid::fromString(cur->data(Qt::UserRole).toString()) : QUuid());
    });
    connect(m_layers, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        if (m_updating)
            return;
        const QUuid id = QUuid::fromString(item->data(Qt::UserRole).toString());
        const bool visible = item->checkState() == Qt::Checked;
        const QString name = item->text();
        m_project->edit(tr("Edit Layer"), [&](ProjectData &d) {
            if (DesignLayer *l = d.design.layer(id)) {
                l->visible = visible;
                l->name = name;
            }
        });
    });

    // ---- Properties dock -----------------------------------------------
    m_props = new QWidget(this);
    auto *pl = new QVBoxLayout(m_props);
    m_propsTitle = new QLabel(m_props);
    QFont tf = m_propsTitle->font();
    tf.setBold(true);
    m_propsTitle->setFont(tf);
    pl->addWidget(m_propsTitle);

    auto *common = new QFormLayout;
    m_opacity = hslider(0, 100, m_props);
    m_scale = new QDoubleSpinBox(m_props);
    m_scale->setRange(1, 5000);
    m_scale->setSuffix(QStringLiteral(" %"));
    m_rotation = new QDoubleSpinBox(m_props);
    m_rotation->setRange(-360, 360);
    m_rotation->setSuffix(QStringLiteral("°"));
    common->addRow(tr("Opacity"), m_opacity);
    common->addRow(tr("Scale"), m_scale);
    common->addRow(tr("Rotation"), m_rotation);
    pl->addLayout(common);

    m_typePages = new QStackedWidget(m_props);
    // Image page
    auto *imgPage = new QWidget(m_typePages);
    auto *imgForm = new QFormLayout(imgPage);
    m_imgBrightness = hslider(-100, 100, imgPage);
    m_imgContrast = hslider(0, 200, imgPage);
    m_imgSaturation = hslider(0, 300, imgPage);
    imgForm->addRow(tr("Brightness"), m_imgBrightness);
    imgForm->addRow(tr("Contrast"), m_imgContrast);
    imgForm->addRow(tr("Saturation"), m_imgSaturation);
    m_typePages->addWidget(imgPage);
    // Text page
    auto *textPage = new QWidget(m_typePages);
    auto *textForm = new QFormLayout(textPage);
    m_text = new QPlainTextEdit(textPage);
    m_text->setMaximumHeight(80);
    m_font = new QFontComboBox(textPage);
    m_fontSize = new QSpinBox(textPage);
    m_fontSize->setRange(6, 1000);
    m_bold = new QCheckBox(tr("Bold"), textPage);
    m_outline = new QDoubleSpinBox(textPage);
    m_outline->setRange(0, 100);
    m_shadow = new QCheckBox(tr("Drop shadow"), textPage);
    textForm->addRow(tr("Text"), m_text);
    textForm->addRow(tr("Font"), m_font);
    textForm->addRow(tr("Size"), m_fontSize);
    textForm->addRow(QString(), m_bold);
    textForm->addRow(tr("Fill"), colorButton(QStringLiteral("textColor"), [](const DesignLayer &l) { return l.textColor; },
                                             [](DesignLayer &l, const QColor &c) { l.textColor = c; }));
    textForm->addRow(tr("Outline"), m_outline);
    textForm->addRow(tr("Outline color"),
                     colorButton(QStringLiteral("outlineColor"), [](const DesignLayer &l) { return l.outlineColor; },
                                 [](DesignLayer &l, const QColor &c) { l.outlineColor = c; }));
    textForm->addRow(QString(), m_shadow);
    m_typePages->addWidget(textPage);
    // Shape page
    auto *shapePage = new QWidget(m_typePages);
    auto *shapeForm = new QFormLayout(shapePage);
    m_shapeKind = new QComboBox(shapePage);
    m_shapeKind->addItems({tr("Rectangle"), tr("Ellipse")});
    m_shapeW = new QSpinBox(shapePage);
    m_shapeH = new QSpinBox(shapePage);
    m_shapeW->setRange(1, 10000);
    m_shapeH->setRange(1, 10000);
    m_radius = new QDoubleSpinBox(shapePage);
    m_radius->setRange(0, 2000);
    shapeForm->addRow(tr("Shape"), m_shapeKind);
    shapeForm->addRow(tr("Width"), m_shapeW);
    shapeForm->addRow(tr("Height"), m_shapeH);
    shapeForm->addRow(tr("Fill"), colorButton(QStringLiteral("fillColor"), [](const DesignLayer &l) { return l.fillColor; },
                                              [](DesignLayer &l, const QColor &c) { l.fillColor = c; }));
    shapeForm->addRow(tr("Corner radius"), m_radius);
    m_typePages->addWidget(shapePage);
    pl->addWidget(m_typePages);
    pl->addStretch();

    // Layers and properties share one dock, split vertically.
    auto *propsScroll = new QScrollArea(this);
    propsScroll->setWidget(m_props);
    propsScroll->setWidgetResizable(true);
    propsScroll->setFrameShape(QFrame::NoFrame);
    auto *split = new QSplitter(Qt::Vertical, this);
    split->addWidget(m_layers);
    split->addWidget(propsScroll);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 2);
    m_layers->setMinimumWidth(260);
    auto *propsDock = new QDockWidget(tr("Layers & Properties"), this);
    propsDock->setObjectName(QStringLiteral("design.panel"));
    propsDock->setWidget(split);
    addDockWidget(Qt::RightDockWidgetArea, propsDock);

    // Property bindings (each property merges into one undo step while dragging).
    connect(m_opacity, &QSlider::valueChanged, this,
            [this](int v) { editLayer(QStringLiteral("opacity"), [v](DesignLayer &l) { l.opacity = v / 100.0; }); });
    connect(m_scale, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        editLayer(QStringLiteral("scale"), [this, v](DesignLayer &l) {
            // Keep the centre fixed while scaling.
            const QRectF r = m_canvas->renderer()->localRect(l);
            const QPointF c = l.position + QPointF(r.width() * l.scale / 2, r.height() * l.scale / 2);
            l.scale = v / 100.0;
            l.position = c - QPointF(r.width() * l.scale / 2, r.height() * l.scale / 2);
        });
    });
    connect(m_rotation, &QDoubleSpinBox::valueChanged, this,
            [this](double v) { editLayer(QStringLiteral("rotation"), [v](DesignLayer &l) { l.rotation = v; }); });
    connect(m_imgBrightness, &QSlider::valueChanged, this,
            [this](int v) { editLayer(QStringLiteral("brightness"), [v](DesignLayer &l) { l.brightness = v / 100.0; }); });
    connect(m_imgContrast, &QSlider::valueChanged, this,
            [this](int v) { editLayer(QStringLiteral("contrast"), [v](DesignLayer &l) { l.contrast = v / 100.0; }); });
    connect(m_imgSaturation, &QSlider::valueChanged, this,
            [this](int v) { editLayer(QStringLiteral("saturation"), [v](DesignLayer &l) { l.saturation = v / 100.0; }); });
    connect(m_text, &QPlainTextEdit::textChanged, this, [this] {
        const QString t = m_text->toPlainText();
        editLayer(QStringLiteral("text"), [t](DesignLayer &l) {
            l.text = t;
            l.name = t.section(QLatin1Char('\n'), 0, 0).left(40);
        });
    });
    connect(m_font, &QFontComboBox::currentFontChanged, this, [this](const QFont &f) {
        editLayer(QStringLiteral("font"), [f](DesignLayer &l) { l.fontFamily = f.family(); });
    });
    connect(m_fontSize, &QSpinBox::valueChanged, this,
            [this](int v) { editLayer(QStringLiteral("fontSize"), [v](DesignLayer &l) { l.fontSize = v; }); });
    connect(m_bold, &QCheckBox::toggled, this,
            [this](bool v) { editLayer(QString(), [v](DesignLayer &l) { l.bold = v; }); });
    connect(m_outline, &QDoubleSpinBox::valueChanged, this,
            [this](double v) { editLayer(QStringLiteral("outline"), [v](DesignLayer &l) { l.outlineWidth = v; }); });
    connect(m_shadow, &QCheckBox::toggled, this,
            [this](bool v) { editLayer(QString(), [v](DesignLayer &l) { l.shadow = v; }); });
    connect(m_shapeKind, &QComboBox::currentIndexChanged, this, [this](int i) {
        editLayer(QString(), [i](DesignLayer &l) { l.shape = i == 1 ? ShapeKind::Ellipse : ShapeKind::Rectangle; });
    });
    connect(m_shapeW, &QSpinBox::valueChanged, this,
            [this](int v) { editLayer(QStringLiteral("w"), [v](DesignLayer &l) { l.shapeSize.setWidth(v); }); });
    connect(m_shapeH, &QSpinBox::valueChanged, this,
            [this](int v) { editLayer(QStringLiteral("h"), [v](DesignLayer &l) { l.shapeSize.setHeight(v); }); });
    connect(m_radius, &QDoubleSpinBox::valueChanged, this,
            [this](double v) { editLayer(QStringLiteral("radius"), [v](DesignLayer &l) { l.cornerRadius = v; }); });

    connect(m_canvas, &DesignCanvas::selectionChanged, this, [this] {
        rebuildLayers();
        refreshProperties();
    });
    connect(m_canvas, &DesignCanvas::editTextRequested, this, [this, propsDock] {
        propsDock->raise();
        m_text->setFocus();
        m_text->selectAll();
    });
    connect(m_canvas, &DesignCanvas::imagesDropped, this, [this](const QStringList &paths, const QPointF &) {
        for (const QString &p : paths) {
            if (MediaProbe::isImageFile(p))
                addImageLayer(p, QFileInfo(p).fileName(), false);
        }
    });
    connect(project, &Project::changed, this, [this] {
        m_canvas->renderer()->clearCache();
        rebuildLayers();
        refreshProperties();
    });
    rebuildLayers();
    refreshProperties();
}

QPushButton *DesignStudio::colorButton(const QString &what, std::function<QColor(const DesignLayer &)> get,
                                       std::function<void(DesignLayer &, const QColor &)> set)
{
    auto *b = new QPushButton(this);
    connect(b, &QPushButton::clicked, this, [this, what, get, set] {
        const DesignLayer *l = m_project->data().design.layer(m_canvas->selectedLayer());
        if (!l)
            return;
        const QColor c = QColorDialog::getColor(get(*l), this, tr("Choose Color"), QColorDialog::ShowAlphaChannel);
        if (c.isValid())
            editLayer(QString(), [set, c](DesignLayer &layer) { set(layer, c); });
    });
    m_colorRefreshers.append([this, b, get] {
        if (const DesignLayer *l = m_project->data().design.layer(m_canvas->selectedLayer()))
            setSwatch(b, get(*l));
    });
    return b;
}

void DesignStudio::editLayer(const QString &what, const std::function<void(DesignLayer &)> &fn)
{
    if (m_updating)
        return;
    const QUuid id = m_canvas->selectedLayer();
    if (id.isNull())
        return;
    m_project->edit(
        tr("Edit Layer"),
        [&](ProjectData &d) {
            if (DesignLayer *l = d.design.layer(id))
                fn(*l);
        },
        what.isEmpty() ? QString() : what + id.toString());
}

void DesignStudio::rebuildLayers()
{
    m_updating = true;
    const DesignDocument &doc = m_project->data().design;
    m_layers->clear();
    // Top-most layer first, like every graphics app.
    for (int i = doc.layers.size() - 1; i >= 0; --i) {
        const DesignLayer &l = doc.layers[i];
        QString prefix;
        switch (l.type) {
        case LayerType::Image: prefix = QStringLiteral("▣  "); break;
        case LayerType::Text: prefix = QStringLiteral("T  "); break;
        case LayerType::Shape: prefix = QStringLiteral("■  "); break;
        }
        auto *item = new QListWidgetItem(l.name.isEmpty() ? prefix + layerTypeToString(l.type) : l.name, m_layers);
        item->setData(Qt::UserRole, l.id.toString());
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable);
        item->setCheckState(l.visible ? Qt::Checked : Qt::Unchecked);
        item->setToolTip(tr("Checkbox: visibility · Double-click: rename"));
        if (l.id == m_canvas->selectedLayer())
            m_layers->setCurrentItem(item);
    }
    // Canvas preset combo + background.
    const QSize size = doc.canvasSize;
    int idx = m_preset->count() - 1;
    for (int i = 0; i < m_preset->count(); ++i) {
        if (m_preset->itemData(i).toSize() == size)
            idx = i;
    }
    m_preset->setCurrentIndex(idx);
    if (idx == m_preset->count() - 1)
        m_preset->setItemText(idx, tr("Custom (%1×%2)…").arg(size.width()).arg(size.height()));
    else
        m_preset->setItemText(m_preset->count() - 1, tr("Custom…"));
    setSwatch(m_bgButton, doc.background);
    m_updating = false;
}

void DesignStudio::refreshProperties()
{
    const DesignLayer *l = m_project->data().design.layer(m_canvas->selectedLayer());
    m_props->setEnabled(l != nullptr);
    m_typePages->setVisible(l != nullptr);
    if (!l) {
        m_propsTitle->setText(tr("Select a layer on the canvas"));
        return;
    }
    m_updating = true;
    m_propsTitle->setText(l->name.isEmpty() ? layerTypeToString(l->type) : l->name);
    m_opacity->setValue(int(std::lround(l->opacity * 100)));
    m_scale->setValue(l->scale * 100);
    m_rotation->setValue(l->rotation);
    switch (l->type) {
    case LayerType::Image:
        m_typePages->setCurrentIndex(0);
        m_imgBrightness->setValue(int(std::lround(l->brightness * 100)));
        m_imgContrast->setValue(int(std::lround(l->contrast * 100)));
        m_imgSaturation->setValue(int(std::lround(l->saturation * 100)));
        break;
    case LayerType::Text:
        m_typePages->setCurrentIndex(1);
        if (m_text->toPlainText() != l->text)
            m_text->setPlainText(l->text);
        if (!l->fontFamily.isEmpty())
            m_font->setCurrentFont(QFont(l->fontFamily));
        m_fontSize->setValue(l->fontSize);
        m_bold->setChecked(l->bold);
        m_outline->setValue(l->outlineWidth);
        m_shadow->setChecked(l->shadow);
        break;
    case LayerType::Shape:
        m_typePages->setCurrentIndex(2);
        m_shapeKind->setCurrentIndex(l->shape == ShapeKind::Ellipse ? 1 : 0);
        m_shapeW->setValue(l->shapeSize.width());
        m_shapeH->setValue(l->shapeSize.height());
        m_radius->setValue(l->cornerRadius);
        break;
    }
    for (const auto &r : m_colorRefreshers)
        r();
    m_updating = false;
}

void DesignStudio::addImageLayer(const QString &path, const QString &name, bool fitCanvas)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize imgSize = reader.size();
    DesignLayer l;
    l.id = QUuid::createUuid();
    l.type = LayerType::Image;
    l.name = name;
    l.imagePath = path;
    const QSize canvas = m_project->data().design.canvasSize;
    if (imgSize.isValid()) {
        const double sx = double(canvas.width()) / imgSize.width();
        const double sy = double(canvas.height()) / imgSize.height();
        // Frames fill the canvas (cover); other images fit inside at most 60%.
        l.scale = fitCanvas ? std::max(sx, sy) : std::min(1.0, std::min(sx, sy) * 0.6);
        l.position = QPointF((canvas.width() - imgSize.width() * l.scale) / 2,
                             (canvas.height() - imgSize.height() * l.scale) / 2);
    }
    m_project->edit(tr("Add Image Layer"), [&](ProjectData &d) {
        // Background frames go to the bottom so existing text stays on top.
        if (fitCanvas)
            d.design.layers.prepend(l);
        else
            d.design.layers.append(l);
    });
    m_canvas->setSelectedLayer(l.id);
}

void DesignStudio::addText()
{
    DesignLayer l;
    l.id = QUuid::createUuid();
    l.type = LayerType::Text;
    l.text = tr("YOUR TITLE");
    l.name = l.text;
    l.fontSize = m_project->data().design.canvasSize.height() / 7;
    l.outlineWidth = std::max(2, l.fontSize / 14);
    const QRectF r = m_canvas->renderer()->localRect(l);
    const QSize canvas = m_project->data().design.canvasSize;
    l.position = QPointF((canvas.width() - r.width()) / 2, (canvas.height() - r.height()) / 2);
    m_project->edit(tr("Add Text"), [&](ProjectData &d) { d.design.layers.append(l); });
    m_canvas->setSelectedLayer(l.id);
}

void DesignStudio::addImageDialog()
{
    QSettings s;
    const QString dir = s.value(QStringLiteral("paths/designImage"),
                                QStandardPaths::writableLocation(QStandardPaths::PicturesLocation))
                            .toString();
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Add Image"), dir, tr("Images (*.png *.jpg *.jpeg *.bmp *.webp *.tif *.tiff *.gif)"));
    if (files.isEmpty())
        return;
    s.setValue(QStringLiteral("paths/designImage"), QFileInfo(files.first()).absolutePath());
    for (const auto &f : files)
        addImageLayer(f, QFileInfo(f).fileName(), false);
}

void DesignStudio::addShape(ShapeKind kind)
{
    DesignLayer l;
    l.id = QUuid::createUuid();
    l.type = LayerType::Shape;
    l.shape = kind;
    l.name = kind == ShapeKind::Ellipse ? tr("Ellipse") : tr("Rectangle");
    const QSize canvas = m_project->data().design.canvasSize;
    l.shapeSize = QSize(canvas.width() / 3, kind == ShapeKind::Ellipse ? canvas.width() / 3 : canvas.height() / 5);
    l.position = QPointF((canvas.width() - l.shapeSize.width()) / 2.0, (canvas.height() - l.shapeSize.height()) / 2.0);
    m_project->edit(tr("Add Shape"), [&](ProjectData &d) { d.design.layers.append(l); });
    m_canvas->setSelectedLayer(l.id);
}

void DesignStudio::deleteSelected()
{
    const QUuid id = m_canvas->selectedLayer();
    if (id.isNull())
        return;
    m_project->edit(tr("Delete Layer"), [&](ProjectData &d) {
        const int i = d.design.indexOf(id);
        if (i >= 0)
            d.design.layers.removeAt(i);
    });
}

void DesignStudio::moveSelected(int delta)
{
    const QUuid id = m_canvas->selectedLayer();
    m_project->edit(delta > 0 ? tr("Bring Forward") : tr("Send Backward"), [&](ProjectData &d) {
        const int i = d.design.indexOf(id);
        const int j = i + delta;
        if (i >= 0 && j >= 0 && j < d.design.layers.size())
            d.design.layers.swapItemsAt(i, j);
    });
}

void DesignStudio::duplicateSelected()
{
    const DesignLayer *src = m_project->data().design.layer(m_canvas->selectedLayer());
    if (!src)
        return;
    DesignLayer copy = *src;
    copy.id = QUuid::createUuid();
    copy.name = src->name + tr(" copy");
    copy.position += QPointF(24, 24);
    m_project->edit(tr("Duplicate Layer"), [&](ProjectData &d) {
        d.design.layers.insert(d.design.indexOf(src->id) + 1, copy);
    });
    m_canvas->setSelectedLayer(copy.id);
}

void DesignStudio::exportImage()
{
    QSettings s;
    const QString dir = s.value(QStringLiteral("paths/designExport"),
                                QStandardPaths::writableLocation(QStandardPaths::PicturesLocation))
                            .toString();
    QString selectedFilter;
    QString path = QFileDialog::getSaveFileName(this, tr("Export Image"), dir + QStringLiteral("/thumbnail.png"),
                                                tr("PNG Image (*.png);;JPEG Image (*.jpg *.jpeg)"), &selectedFilter);
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += selectedFilter.startsWith(QLatin1String("JPEG")) ? QStringLiteral(".jpg") : QStringLiteral(".png");
    s.setValue(QStringLiteral("paths/designExport"), QFileInfo(path).absolutePath());

    QImage img = m_canvas->renderer()->renderToImage(m_project->data().design);
    const bool jpeg = path.endsWith(QLatin1String(".jpg"), Qt::CaseInsensitive)
        || path.endsWith(QLatin1String(".jpeg"), Qt::CaseInsensitive);
    if (jpeg)
        img = img.convertToFormat(QImage::Format_RGB32);
    if (img.save(path, nullptr, jpeg ? 92 : -1))
        emit status(tr("Exported %1 (%2×%3)").arg(QFileInfo(path).fileName()).arg(img.width()).arg(img.height()));
    else
        emit status(tr("Could not write %1").arg(path));
}

} // namespace spotlight
