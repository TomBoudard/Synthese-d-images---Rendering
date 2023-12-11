#include "glshaderwindow.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QPixmap>
#include <QScreen>
#include <QQuaternion>
#include <QOpenGLFramebufferObjectFormat>
// Buttons/sliders for User interface:
#include <QGroupBox>
#include <QCheckBox>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QLabel>
// Layouts for User interface
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QDebug>
#include <assert.h>

#include <cmath>

#include <QTime>

#include "perlinNoise.h" // defines tables for Perlin Noise

#include "bvh/bvh.hpp"
#include "bvh/triangle.hpp"

glShaderWindow::glShaderWindow(QWindow *parent)
// Initialize obvious default values here (e.g. 0 for pointers)
    : OpenGLWindow(parent), modelMesh(0),
      m_program(0), compute_program(0), caustics_program(0), shadowMapGenerationProgram(0),
      g_vertices(0), g_normals(0), g_texcoords(0), g_colors(0), g_indices(0),
      gpgpu_vertices(0), gpgpu_normals(0), gpgpu_texcoords(0), gpgpu_colors(0), gpgpu_indices(0),
      environmentMap(0), texture(0), textureNormal(0), useNormalMap(false), permTexture(0), pixels(0), mouseButton(Qt::NoButton), auxWidget(0),
      isGPGPU(true), hasComputeShaders(true), blinnPhong(true), transparent(false), etaReal({1.5, 1.5, 1.5}), etaImag({0., 0., 0.}), lightIntensity({1., 1., 1.}), lightIntensityHSV({0., 0., 1.}), shininess(50.0f), lightDistance(5.0f), groundDistance(0.78),
      shadowMap_fboId(0), shadowMap_rboId(0), shadowMap_textureId(0), fullScreenSnapshots(false), causticsResult(0), computeResult(0), gammaCorrection(false), participatingEnv(false), maxTrace(7),
      m_indexBuffer(QOpenGLBuffer::IndexBuffer), ground_indexBuffer(QOpenGLBuffer::IndexBuffer)
{
    // Default values you might want to tinker with
    shadowMapDimension = 2048;
    // Group size for compute shaders
    compute_groupsize_x = 8;
    compute_groupsize_y = 8;

    timer = startTimer(200);

    // halton2 = halton_sequence(2, HALTON_SIZE);
    // halton3 = halton_sequence(3, HALTON_SIZE);

    m_fragShaderSuffix << "*.frag" << "*.fs";
    m_vertShaderSuffix << "*.vert" << "*.vs";
    m_compShaderSuffix << "*.comp" << "*.cs";
}

glShaderWindow::~glShaderWindow()
{
    if (modelMesh) delete modelMesh;
    if (m_program) {
        m_program->release();
        delete m_program;
    }
    if (shadowMapGenerationProgram) {
        shadowMapGenerationProgram->release();
        delete shadowMapGenerationProgram;
    }
    if (compute_program) {
        compute_program->release();
        delete compute_program;
    }
    if (caustics_program) {
        caustics_program->release();
        delete caustics_program;
    }
    if (shadowMap_textureId) glDeleteTextures(1, &shadowMap_textureId);
    if (shadowMap_fboId) glDeleteFramebuffers(1, &shadowMap_fboId);
    if (shadowMap_rboId) glDeleteRenderbuffers(1, &shadowMap_rboId);
    if (pixels) delete [] pixels;
    m_vertexBuffer.release();
    m_vertexBuffer.destroy();
    m_indexBuffer.release();
    m_indexBuffer.destroy();
    m_colorBuffer.release();
    m_colorBuffer.destroy();
    m_normalBuffer.release();
    m_normalBuffer.destroy();
    m_texcoordBuffer.release();
    m_texcoordBuffer.destroy();
    m_vao.release();
    m_vao.destroy();
    ground_vertexBuffer.release();
    ground_vertexBuffer.destroy();
    ground_indexBuffer.release();
    ground_indexBuffer.destroy();
    ground_colorBuffer.release();
    ground_colorBuffer.destroy();
    ground_normalBuffer.release();
    ground_normalBuffer.destroy();
    ground_texcoordBuffer.release();
    ground_texcoordBuffer.destroy();
    ground_vao.release();
    ground_vao.destroy();
    if (g_vertices) delete [] g_vertices;
    if (g_colors) delete [] g_colors;
    if (g_normals) delete [] g_normals;
    if (g_indices) delete [] g_indices;
    if (gpgpu_vertices) delete [] gpgpu_vertices;
    if (gpgpu_colors) delete [] gpgpu_colors;
    if (gpgpu_normals) delete [] gpgpu_normals;
    if (gpgpu_indices) delete [] gpgpu_indices;
}


void glShaderWindow::openSceneFromFile() {
    QFileDialog dialog(0, "Open Scene", workingDirectory, "*.off *.obj *.ply *.3ds");
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    QString filename;
    int ret = dialog.exec();
    if (ret == QDialog::Accepted) {
        workingDirectory = dialog.directory().path() + "/";
        modelName = dialog.selectedFiles()[0];
    }

    if (!modelName.isNull())
    {
        openScene();
        resetFrame();
        renderNow();
    }
}

void glShaderWindow::openNewTexture() {
    QFileDialog dialog(0, "Open texture image", workingDirectory + "../textures/", "*.png *.PNG *.jpg *.JPG *.tif *.TIF");
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    QString filename;
    int ret = dialog.exec();
    if (ret == QDialog::Accepted) {
        textureName = dialog.selectedFiles()[0];
        if (!textureName.isNull()) {
            if (texture) {
                texture->release();
                texture->destroy();
                delete texture;
                texture = 0;
            }
			glActiveTexture(GL_TEXTURE0);
			// the shader wants a texture. We load one.
			texture = new QOpenGLTexture(QImage(textureName));
			if (texture) {
				texture->setWrapMode(QOpenGLTexture::Repeat);
				texture->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
				texture->setMagnificationFilter(QOpenGLTexture::Linear);
				texture->bind(0);
            }
        }
        resetFrame();
        renderNow();
        textureNormalName = textureName.left(textureName.size() - 4) + "_normal" + textureName.right(4);
        if (!textureNormalName.isNull()) {
            if (textureNormal) {
                textureNormal->release();
                textureNormal->destroy();
                delete textureNormal;
                textureNormal = 0;
            }
			glActiveTexture(GL_TEXTURE4);
			// the shader wants a texture. We load one.
			textureNormal = new QOpenGLTexture(QImage(textureNormalName));
			if (textureNormal) {
				textureNormal->setWrapMode(QOpenGLTexture::Repeat);
				textureNormal->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
				textureNormal->setMagnificationFilter(QOpenGLTexture::Linear);
				textureNormal->bind(4);
            }
        }
        resetFrame();
        renderNow();
    }
}


void glShaderWindow::openNewEnvMap() {
    QFileDialog dialog(0, "Open environment map image", workingDirectory + "../textures/", "*.png *.PNG *.jpg *.JPG");
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    QString filename;
    int ret = dialog.exec();
    if (ret == QDialog::Accepted) {
        envMapName= dialog.selectedFiles()[0];
        if (environmentMap) {
            environmentMap->release();
            environmentMap->destroy();
            delete environmentMap;
            environmentMap = 0;
        }
		glActiveTexture(GL_TEXTURE1);
        environmentMap = new QOpenGLTexture(QImage(envMapName).mirrored());
        if (environmentMap) {
            environmentMap->setWrapMode(QOpenGLTexture::MirroredRepeat);
            environmentMap->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
            environmentMap->setMagnificationFilter(QOpenGLTexture::Nearest);
            environmentMap->bind(1);
        }
        resetFrame();
        renderNow();
    }
}

void glShaderWindow::cookTorranceClicked()
{
    blinnPhong = false;
    resetFrame();
    renderNow();
}

void glShaderWindow::blinnPhongClicked()
{
    blinnPhong = true;
    resetFrame();
    renderNow();
}

void glShaderWindow::transparentClicked()
{
    transparent = true;
    resetFrame();
    renderNow();
}

void glShaderWindow::opaqueClicked()
{
    transparent = false;
    resetFrame();
    renderNow();
}

void glShaderWindow::hsvToRGB(float* hsv, float* rgb)
{
    int i = static_cast<int>(std::floor(hsv[0] / 60));
    float c = hsv[2] * hsv[1];
    float x = c * (1 - std::abs(std::fmod((hsv[0] / 60.0), 2.0) - 1.));
    float m = hsv[2] - c;

    float rgbprime[3];

    switch (i % 6) {
        case 0: rgbprime[0] = c; rgbprime[1] = x; rgbprime[2] = 0; break;
        case 1: rgbprime[0] = x; rgbprime[1] = c; rgbprime[2] = 0; break;
        case 2: rgbprime[0] = 0; rgbprime[1] = c; rgbprime[2] = x; break;
        case 3: rgbprime[0] = 0; rgbprime[1] = x; rgbprime[2] = c; break;
        case 4: rgbprime[0] = x; rgbprime[1] = 0; rgbprime[2] = c; break;
        case 5: rgbprime[0] = c; rgbprime[1] = 0; rgbprime[2] = x; break;
    }

    rgb[0] = rgbprime[0] + m;
    rgb[1] = rgbprime[1] + m;
    rgb[2] = rgbprime[2] + m;
}

void glShaderWindow::updateLightIntensityH(double lightValueH)
{
    lightIntensityHSV[0] = lightValueH;
    hsvToRGB(lightIntensityHSV, lightIntensity);
    resetFrame();
    renderNow();
}

void glShaderWindow::updateLightIntensityS(double lightValueS)
{
    lightIntensityHSV[1] = lightValueS;
    hsvToRGB(lightIntensityHSV, lightIntensity);
    resetFrame();
    renderNow();
}

void glShaderWindow::updateLightIntensityV(double lightValueV)
{
    lightIntensityHSV[2] = lightValueV;
    hsvToRGB(lightIntensityHSV, lightIntensity);
    resetFrame();
    renderNow();
}

void glShaderWindow::updateShininess(int shininessSliderValue)
{
    shininess = shininessSliderValue;
    resetFrame();
    renderNow();
}

void glShaderWindow::updateEtaRealR(double etaRealR)
{
    etaReal[0] = etaRealR;
    resetFrame();
    renderNow();
}

void glShaderWindow::updateEtaRealG(double etaRealG)
{
    etaReal[1] = etaRealG;
    resetFrame();
    renderNow();
}

void glShaderWindow::updateEtaRealB(double etaRealB)
{
    etaReal[2] = etaRealB;
    resetFrame();
    renderNow();
}

void glShaderWindow::updateEtaImagR(double etaImagR)
{
    etaImag[0] = etaImagR;
    resetFrame();
    renderNow();
}

void glShaderWindow::updateEtaImagG(double etaImagG)
{
    etaImag[1] = etaImagG;
    resetFrame();
    renderNow();
}

void glShaderWindow::updateEtaImagB(double etaImagB)
{
    etaImag[2] = etaImagB;
    resetFrame();
    renderNow();
}

void glShaderWindow::updateGammaCorrection(int state)
{
    if (state == Qt::Checked) {
        gammaCorrection = true;
    } else {
        gammaCorrection = false;
    }
    resetFrame();
    renderNow();
}
void glShaderWindow::updateUseNormalMap(int state)
{
    if (state == Qt::Checked) {
        useNormalMap = true;
    } else {
        useNormalMap = false;
    }
    resetFrame();
    renderNow();
}

void glShaderWindow::updateParticipatingEnv(int state)
{
    if (state == Qt::Checked) {
        participatingEnv = true;
    } else {
        participatingEnv = false;
    }
    resetFrame();
    renderNow();
}

void glShaderWindow::updateMaxTrace(int maxTr)
{
    maxTrace = maxTr;
    resetFrame();
    renderNow();
}


QWidget *glShaderWindow::makeAuxWindow()
{
    if (auxWidget)
        if (auxWidget->isVisible()) return auxWidget;
    auxWidget = new QWidget;

    QVBoxLayout *outer = new QVBoxLayout;

    QHBoxLayout* checkboxAndSliderLayout = new QHBoxLayout;

    QVBoxLayout* checkboxLayout = new QVBoxLayout;

    QCheckBox* gammaCorrectionCheckbox = new QCheckBox("Gamma Correction");
    gammaCorrectionCheckbox->setChecked(gammaCorrection);
    connect(gammaCorrectionCheckbox, SIGNAL(stateChanged(int)), this, SLOT(updateGammaCorrection(int)));
    checkboxLayout->addWidget(gammaCorrectionCheckbox);

    QCheckBox* useNormalMapCheckBox = new QCheckBox("Use Normal Map");
    useNormalMapCheckBox->setChecked(useNormalMap);
    connect(useNormalMapCheckBox, SIGNAL(stateChanged(int)), this, SLOT(updateUseNormalMap(int)));
    checkboxLayout->addWidget(useNormalMapCheckBox);

    QCheckBox* participatingEnvMapCheckBox = new QCheckBox("Sous l'ocean");
    participatingEnvMapCheckBox->setChecked(participatingEnv);
    connect(participatingEnvMapCheckBox, SIGNAL(stateChanged(int)), this, SLOT(updateParticipatingEnv(int)));
    checkboxLayout->addWidget(participatingEnvMapCheckBox);

    checkboxAndSliderLayout->addLayout(checkboxLayout);

    QSlider* maxTraceSlider = new QSlider(Qt::Vertical);
    maxTraceSlider->setTickPosition(QSlider::TicksBelow);
    maxTraceSlider->setMinimum(0);
    maxTraceSlider->setMaximum(20);
    maxTraceSlider->setSliderPosition(maxTrace);
    connect(maxTraceSlider,SIGNAL(valueChanged(int)),this,SLOT(updateMaxTrace(int)));
    QLabel* maxTraceLabel = new QLabel("Max Trace = ");
    QLabel* maxTraceLabelValue = new QLabel();
    maxTraceLabelValue->setNum(maxTrace);
    connect(maxTraceSlider,SIGNAL(valueChanged(int)),maxTraceLabelValue,SLOT(setNum(int)));
    QHBoxLayout *hboxMaxTrace = new QHBoxLayout;
    hboxMaxTrace->addWidget(maxTraceLabel);
    hboxMaxTrace->addWidget(maxTraceLabelValue);
    hboxMaxTrace->addWidget(maxTraceSlider);

    checkboxAndSliderLayout->addLayout(hboxMaxTrace);

    outer->addLayout(checkboxAndSliderLayout);

    QHBoxLayout *buttons = new QHBoxLayout;

    QGroupBox *groupBox = new QGroupBox("Specular Model selection");
    QRadioButton *radio1 = new QRadioButton("Blinn-Phong");
    QRadioButton *radio2 = new QRadioButton("Cook-Torrance");
    if (blinnPhong) radio1->setChecked(true);
    else radio1->setChecked(true);
    connect(radio1, SIGNAL(clicked()), this, SLOT(blinnPhongClicked()));
    connect(radio2, SIGNAL(clicked()), this, SLOT(cookTorranceClicked()));

    QVBoxLayout *vbox = new QVBoxLayout;
    vbox->addWidget(radio1);
    vbox->addWidget(radio2);
    groupBox->setLayout(vbox);
    buttons->addWidget(groupBox);

    QGroupBox *groupBox2 = new QGroupBox("Surface:");
    QRadioButton *transparent1 = new QRadioButton("&Transparent");
    QRadioButton *transparent2 = new QRadioButton("&Opaque");
    if (transparent) transparent1->setChecked(true);
    else transparent2->setChecked(true);
    connect(transparent1, SIGNAL(clicked()), this, SLOT(transparentClicked()));
    connect(transparent2, SIGNAL(clicked()), this, SLOT(opaqueClicked()));
    QVBoxLayout *vbox2 = new QVBoxLayout;
    vbox2->addWidget(transparent1);
    vbox2->addWidget(transparent2);
    groupBox2->setLayout(vbox2);
    buttons->addWidget(groupBox2);
    outer->addLayout(buttons);

    // light source intensity
    QDoubleSpinBox* lightSpinBoxR = new QDoubleSpinBox();
    QDoubleSpinBox* lightSpinBoxG = new QDoubleSpinBox();
    QDoubleSpinBox* lightSpinBoxB = new QDoubleSpinBox();
    lightSpinBoxR->setValue(lightIntensityHSV[0]);
    lightSpinBoxG->setValue(lightIntensityHSV[1]);
    lightSpinBoxB->setValue(lightIntensityHSV[2]);
    lightSpinBoxR->setRange(0, 360);
    lightSpinBoxG->setRange(0, 1);
    lightSpinBoxB->setRange(0, 20);
    lightSpinBoxR->setSingleStep(1);
    lightSpinBoxG->setSingleStep(0.05);
    lightSpinBoxB->setSingleStep(0.1);
    connect(lightSpinBoxR, SIGNAL(valueChanged(double)), this, SLOT(updateLightIntensityH(double)));
    connect(lightSpinBoxG, SIGNAL(valueChanged(double)), this, SLOT(updateLightIntensityS(double)));
    connect(lightSpinBoxB, SIGNAL(valueChanged(double)), this, SLOT(updateLightIntensityV(double)));
    QHBoxLayout *hboxLightLabels = new QHBoxLayout;
    QLabel* lightLabelR = new QLabel("H");
    QLabel* lightLabelG = new QLabel("S");
    QLabel* lightLabelB = new QLabel("V");
    lightLabelR->setAlignment(Qt::AlignCenter);
    lightLabelG->setAlignment(Qt::AlignCenter);
    lightLabelB->setAlignment(Qt::AlignCenter);
    hboxLightLabels->addWidget(lightLabelR);
    hboxLightLabels->addWidget(lightLabelG);
    hboxLightLabels->addWidget(lightLabelB);
    QHBoxLayout *hboxSpinnersLight = new QHBoxLayout;
    hboxSpinnersLight->addWidget(lightSpinBoxR);
    hboxSpinnersLight->addWidget(lightSpinBoxG);
    hboxSpinnersLight->addWidget(lightSpinBoxB);
    QGroupBox *lightGroupBox = new QGroupBox("Light Intensity");
    QVBoxLayout *lightVBox = new QVBoxLayout;
    lightVBox->addLayout(hboxLightLabels);
    lightVBox->addLayout(hboxSpinnersLight);
    lightGroupBox->setLayout(lightVBox);
    outer->addWidget(lightGroupBox);

    // Phong shininess slider
    QSlider* shininessSlider = new QSlider(Qt::Horizontal);
    shininessSlider->setTickPosition(QSlider::TicksBelow);
    shininessSlider->setMinimum(0);
    shininessSlider->setMaximum(200);
    shininessSlider->setSliderPosition(shininess);
    connect(shininessSlider,SIGNAL(valueChanged(int)),this,SLOT(updateShininess(int)));
    QLabel* shininessLabel = new QLabel("Phong exponent = ");
    QLabel* shininessLabelValue = new QLabel();
    shininessLabelValue->setNum(shininess);
    connect(shininessSlider,SIGNAL(valueChanged(int)),shininessLabelValue,SLOT(setNum(int)));
    QHBoxLayout *hboxShininess = new QHBoxLayout;
    hboxShininess->addWidget(shininessLabel);
    hboxShininess->addWidget(shininessLabelValue);
    outer->addLayout(hboxShininess);
    outer->addWidget(shininessSlider);

    // Eta
    QDoubleSpinBox* etaRealSpinBoxR = new QDoubleSpinBox();
    QDoubleSpinBox* etaRealSpinBoxG = new QDoubleSpinBox();
    QDoubleSpinBox* etaRealSpinBoxB = new QDoubleSpinBox();
    etaRealSpinBoxR->setValue(etaReal[0]);
    etaRealSpinBoxG->setValue(etaReal[1]);
    etaRealSpinBoxB->setValue(etaReal[2]);
    etaRealSpinBoxR->setRange(0, 10);
    etaRealSpinBoxG->setRange(0, 10);
    etaRealSpinBoxB->setRange(0, 10);
    etaRealSpinBoxR->setSingleStep(0.1);
    etaRealSpinBoxG->setSingleStep(0.1);
    etaRealSpinBoxB->setSingleStep(0.1);
    connect(etaRealSpinBoxR, SIGNAL(valueChanged(double)), this, SLOT(updateEtaRealR(double)));
    connect(etaRealSpinBoxG, SIGNAL(valueChanged(double)), this, SLOT(updateEtaRealG(double)));
    connect(etaRealSpinBoxB, SIGNAL(valueChanged(double)), this, SLOT(updateEtaRealB(double)));

    QDoubleSpinBox* etaImagSpinBoxR = new QDoubleSpinBox();
    QDoubleSpinBox* etaImagSpinBoxG = new QDoubleSpinBox();
    QDoubleSpinBox* etaImagSpinBoxB = new QDoubleSpinBox();
    etaImagSpinBoxR->setValue(etaImag[0]);
    etaImagSpinBoxG->setValue(etaImag[1]);
    etaImagSpinBoxB->setValue(etaImag[2]);
    etaImagSpinBoxR->setRange(0, 10);
    etaImagSpinBoxG->setRange(0, 10);
    etaImagSpinBoxB->setRange(0, 10);
    etaImagSpinBoxR->setSingleStep(0.1);
    etaImagSpinBoxG->setSingleStep(0.1);
    etaImagSpinBoxB->setSingleStep(0.1);
    connect(etaImagSpinBoxR, SIGNAL(valueChanged(double)), this, SLOT(updateEtaImagR(double)));
    connect(etaImagSpinBoxG, SIGNAL(valueChanged(double)), this, SLOT(updateEtaImagG(double)));
    connect(etaImagSpinBoxB, SIGNAL(valueChanged(double)), this, SLOT(updateEtaImagB(double)));

    QHBoxLayout *hboxEtaLabels = new QHBoxLayout;
    QLabel* etaLabeleps = new QLabel("");
    QLabel* etaLabelR = new QLabel("R");
    QLabel* etaLabelG = new QLabel("G");
    QLabel* etaLabelB = new QLabel("B");
    etaLabeleps->setFixedWidth(20);
    etaLabelR->setAlignment(Qt::AlignCenter);
    etaLabelG->setAlignment(Qt::AlignCenter);
    etaLabelB->setAlignment(Qt::AlignCenter);

    hboxEtaLabels->addWidget(etaLabeleps);
    hboxEtaLabels->addWidget(etaLabelR);
    hboxEtaLabels->addWidget(etaLabelG);
    hboxEtaLabels->addWidget(etaLabelB);

    QHBoxLayout *hboxSpinnersEtaReal = new QHBoxLayout;

    QLabel* etaRealLabel = new QLabel("n");
    etaRealLabel->setFixedWidth(20);
    hboxSpinnersEtaReal->addWidget(etaRealLabel);
    hboxSpinnersEtaReal->addWidget(etaRealSpinBoxR);
    hboxSpinnersEtaReal->addWidget(etaRealSpinBoxG);
    hboxSpinnersEtaReal->addWidget(etaRealSpinBoxB);

    QHBoxLayout *hboxSpinnersEtaImag = new QHBoxLayout;

    QLabel* etaImagLabel = new QLabel("k");
    etaImagLabel->setFixedWidth(20);
    hboxSpinnersEtaImag->addWidget(etaImagLabel);
    hboxSpinnersEtaImag->addWidget(etaImagSpinBoxR);
    hboxSpinnersEtaImag->addWidget(etaImagSpinBoxG);
    hboxSpinnersEtaImag->addWidget(etaImagSpinBoxB);

    QGroupBox *etaGroupBox = new QGroupBox("Eta");
    QVBoxLayout *vboxEta = new QVBoxLayout;
    vboxEta->addLayout(hboxEtaLabels);
    vboxEta->addLayout(hboxSpinnersEtaReal);
    vboxEta->addLayout(hboxSpinnersEtaImag);
    etaGroupBox->setLayout(vboxEta);
    outer->addWidget(etaGroupBox);

    auxWidget->setLayout(outer);
    return auxWidget;
}

void glShaderWindow::createSSBO() 
{
    /* build BVH */
    BVH bvh{ modelMesh->vertices, modelMesh->faces };
    const std::vector<BVH::Node>& nodes = bvh.getNodes();
    const std::vector<Triangle>& triangles = bvh.getTriangles();

	glGenBuffers(5, ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo[0]);
    // TODO: test if 4 float alignment works better
    glBufferData(GL_SHADER_STORAGE_BUFFER, modelMesh->vertices.size() * sizeof(trimesh::point), &(modelMesh->vertices.front()), GL_STATIC_READ);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo[1]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, modelMesh->normals.size() * sizeof(trimesh::vec), &(modelMesh->normals.front()), GL_STATIC_READ);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo[2]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, modelMesh->colors.size() * sizeof(trimesh::Color), &(modelMesh->colors.front()), GL_STATIC_READ);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo[3]);
    // glBufferData(GL_SHADER_STORAGE_BUFFER, modelMesh->faces.size() * 3 * sizeof(int), &(modelMesh->faces.front()), GL_STATIC_READ);
    glBufferData(GL_SHADER_STORAGE_BUFFER, triangles.size() * sizeof(Triangle), triangles.data(), GL_STATIC_READ);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo[4]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, nodes.size() * sizeof(BVH::Node), nodes.data(), GL_STATIC_READ);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    compute_program->bind();
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo[0]);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, ssbo[1]);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, ssbo[2]);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, ssbo[3]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, ssbo[4]);
    caustics_program->bind();
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo[0]);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, ssbo[1]);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, ssbo[2]);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, ssbo[3]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, ssbo[4]);
}

void glShaderWindow::bindSceneToProgram()
{
    // Now create the VAO for the model
    m_vao.bind();
    // If we're doing GPGPU, the VAO is very simple: 4 vertices for a large square covering the screen.
    if (isGPGPU) {
        m_numFaces = 2;
        // Allocate and fill only once
        if (gpgpu_vertices == 0)
        {
            gpgpu_vertices = new trimesh::point[4];
            if (gpgpu_normals == 0) gpgpu_normals = new trimesh::vec[4];
            if (gpgpu_colors == 0) gpgpu_colors = new trimesh::point[4];
            if (gpgpu_texcoords == 0) gpgpu_texcoords = new trimesh::vec2[4];
            if (gpgpu_indices == 0) gpgpu_indices = new int[6];
            gpgpu_vertices[0] = trimesh::point(-1, -1, 0, 1);
            gpgpu_vertices[1] = trimesh::point(-1, 1, 0, 1);
            gpgpu_vertices[2] = trimesh::point(1, -1, 0, 1);
            gpgpu_vertices[3] = trimesh::point(1, 1, 0, 1);
            for (int i = 0; i < 4; i++) {
                gpgpu_normals[i] = trimesh::point(0, 0, -1, 0);
                gpgpu_colors[i] = trimesh::point(0, 0, 1, 1);
            }
            gpgpu_texcoords[0] = trimesh::vec2(0, 0);
            gpgpu_texcoords[1] = trimesh::vec2(0, 1);
            gpgpu_texcoords[2] = trimesh::vec2(1, 0);
            gpgpu_texcoords[3] = trimesh::vec2(1, 1);
            gpgpu_indices[0] = 0;
            gpgpu_indices[1] = 2;
            gpgpu_indices[2] = 1;
            gpgpu_indices[3] = 1;
            gpgpu_indices[4] = 2;
            gpgpu_indices[5] = 3;
        }
    } else m_numFaces = modelMesh->faces.size();

    m_vertexBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
    m_vertexBuffer.bind();
    if (!isGPGPU) m_vertexBuffer.allocate(&(modelMesh->vertices.front()), modelMesh->vertices.size() * sizeof(trimesh::point));
    else m_vertexBuffer.allocate(gpgpu_vertices, 4 * sizeof(trimesh::point));

    m_indexBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
    m_indexBuffer.bind();
    if (!isGPGPU) m_indexBuffer.allocate(&(modelMesh->faces.front()), m_numFaces * 3 * sizeof(int));
    else m_indexBuffer.allocate(gpgpu_indices, m_numFaces * 3 * sizeof(int));

    if (modelMesh->colors.size() > 0) {
        m_colorBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
        m_colorBuffer.bind();
        if (!isGPGPU) m_colorBuffer.allocate(&(modelMesh->colors.front()), modelMesh->colors.size() * sizeof(trimesh::Color));
        else m_colorBuffer.allocate(gpgpu_colors, 4 * sizeof(trimesh::Color));
    }

    m_normalBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
    m_normalBuffer.bind();
    if (!isGPGPU) m_normalBuffer.allocate(&(modelMesh->normals.front()), modelMesh->normals.size() * sizeof(trimesh::vec));
    else m_normalBuffer.allocate(gpgpu_normals, 4 * sizeof(trimesh::vec));

    if ((modelMesh->texcoords.size() > 0) || isGPGPU) {
        m_texcoordBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
        m_texcoordBuffer.bind();
        if (!isGPGPU) m_texcoordBuffer.allocate(&(modelMesh->texcoords.front()), modelMesh->texcoords.size() * sizeof(trimesh::vec2));
        else m_texcoordBuffer.allocate(gpgpu_texcoords, 4 * sizeof(trimesh::vec2));
    }
    m_program->bind();
    // Enable the "vertex" attribute to bind it to our vertex buffer
    m_vertexBuffer.bind();
    m_program->setAttributeBuffer( "vertex", GL_FLOAT, 0, 4 );
    m_program->enableAttributeArray( "vertex" );

    // Enable the "color" attribute to bind it to our colors buffer
    if (modelMesh->colors.size() > 0) {
        m_colorBuffer.bind();
        m_program->setAttributeBuffer( "color", GL_FLOAT, 0, 4 );
        m_program->enableAttributeArray( "color" );
        m_program->setUniformValue("noColor", false);
    } else {
        m_program->setUniformValue("noColor", true);
    }
    m_normalBuffer.bind();
    m_program->setAttributeBuffer( "normal", GL_FLOAT, 0, 4 );
    m_program->enableAttributeArray( "normal" );

    if ((modelMesh->texcoords.size() > 0) || isGPGPU){
        m_texcoordBuffer.bind();
        m_program->setAttributeBuffer( "texcoords", GL_FLOAT, 0, 2 );
        m_program->enableAttributeArray( "texcoords" );
    }
    m_program->release();
    shadowMapGenerationProgram->bind();
    // Enable the "vertex" attribute to bind it to our vertex buffer
    m_vertexBuffer.bind();
    shadowMapGenerationProgram->setAttributeBuffer( "vertex", GL_FLOAT, 0, 4 );
    shadowMapGenerationProgram->enableAttributeArray( "vertex" );
    if (modelMesh->colors.size() > 0) {
        m_colorBuffer.bind();
        shadowMapGenerationProgram->setAttributeBuffer( "color", GL_FLOAT, 0, 4 );
        shadowMapGenerationProgram->enableAttributeArray( "color" );
    }
    m_normalBuffer.bind();
    shadowMapGenerationProgram->setAttributeBuffer( "normal", GL_FLOAT, 0, 4 );
    shadowMapGenerationProgram->enableAttributeArray( "normal" );
    if (modelMesh->texcoords.size() > 0) {
        m_texcoordBuffer.bind();
        shadowMapGenerationProgram->setAttributeBuffer( "texcoords", GL_FLOAT, 0, 2 );
        shadowMapGenerationProgram->enableAttributeArray( "texcoords" );
    }
    shadowMapGenerationProgram->release();
    m_vao.release();

    // Bind ground VAO to ground program as well
    // We create a VAO for the ground from scratch
    ground_vao.bind();
    ground_vertexBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
    ground_vertexBuffer.bind();
    trimesh::point center = modelMesh->bsphere.center;
    float radius = modelMesh->bsphere.r;

    int numR = 10;
    int numTh = 20;
    g_numPoints = numR * numTh;
    // Allocate once, fill in for every new model.
    if (g_vertices == 0) g_vertices = new trimesh::point[g_numPoints];
    if (g_normals == 0) g_normals = new trimesh::vec[g_numPoints];
    if (g_colors == 0) g_colors = new trimesh::point[g_numPoints];
    if (g_texcoords == 0) g_texcoords = new trimesh::vec2[g_numPoints];
    if (g_indices == 0) g_indices = new int[6 * g_numPoints];
    for (int i = 0; i < numR; i++) {
        for (int j = 0; j < numTh; j++) {
            int p = i + j * numR;
            g_normals[p] = trimesh::point(0, 1, 0, 0);
            g_colors[p] = trimesh::point(0.6, 0.85, 0.9, 1);
            float theta = (float)j * 2 * M_PI / numTh;
            float rad =  5.0 * radius * (float) i / numR;
            g_vertices[p] = center + trimesh::point(rad * cos(theta), - groundDistance * radius, rad * sin(theta), 0);
            rad =  5.0 * (float) i / numR;
            g_texcoords[p] = trimesh::vec2(rad * cos(theta), rad * sin(theta));
        }
    }
    ground_vertexBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
    ground_vertexBuffer.bind();
    ground_vertexBuffer.allocate(g_vertices, g_numPoints * sizeof(trimesh::point));
    ground_normalBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
    ground_normalBuffer.bind();
    ground_normalBuffer.allocate(g_normals, g_numPoints * sizeof(trimesh::vec));
    ground_colorBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
    ground_colorBuffer.bind();
    ground_colorBuffer.allocate(g_colors, g_numPoints * sizeof(trimesh::point));
    ground_texcoordBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
    ground_texcoordBuffer.bind();
    ground_texcoordBuffer.allocate(g_texcoords, g_numPoints * sizeof(trimesh::vec2));

    g_numIndices = 0;
    for (int i = 0; i < numR - 1; i++) {
        for (int j = 0; j < numTh; j++) {
            int j_1 = (j + 1) % numTh;
            g_indices[g_numIndices++] = i + j * numR;
            g_indices[g_numIndices++] = i + 1 + j_1 * numR;
            g_indices[g_numIndices++] = i + 1 + j * numR;
            g_indices[g_numIndices++] = i + j * numR;
            g_indices[g_numIndices++] = i + j_1 * numR;
            g_indices[g_numIndices++] = i + 1 + j_1 * numR;
        }

    }
    ground_indexBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
    ground_indexBuffer.bind();
    ground_indexBuffer.allocate(g_indices, g_numIndices * sizeof(int));

    // Also bind the ground to the shadow mapping program:
    shadowMapGenerationProgram->bind();
    ground_vertexBuffer.bind();
    shadowMapGenerationProgram->setAttributeBuffer( "vertex", GL_FLOAT, 0, 4 );
    shadowMapGenerationProgram->enableAttributeArray( "vertex" );
    shadowMapGenerationProgram->release();
    ground_colorBuffer.bind();
    shadowMapGenerationProgram->setAttributeBuffer( "color", GL_FLOAT, 0, 4 );
    shadowMapGenerationProgram->enableAttributeArray( "color" );
    ground_normalBuffer.bind();
    shadowMapGenerationProgram->setAttributeBuffer( "normal", GL_FLOAT, 0, 4 );
    shadowMapGenerationProgram->enableAttributeArray( "normal" );
    ground_texcoordBuffer.bind();
    shadowMapGenerationProgram->setAttributeBuffer( "texcoords", GL_FLOAT, 0, 2 );
    shadowMapGenerationProgram->enableAttributeArray( "texcoords" );
    ground_vao.release();
}

void glShaderWindow::initializeTransformForScene()
{
    // Set standard transformation and light source
    float radius = modelMesh->bsphere.r;
    m_perspective.setToIdentity();
    m_perspective.perspective(45, (float)width()/height(), 0.1 * radius, 20 * radius);
    QVector3D eye = m_center + 2 * radius * QVector3D(0,0,1);
    m_matrix[0].setToIdentity();
    m_matrix[1].setToIdentity();
    m_matrix[2].setToIdentity();
    m_matrix[0].lookAt(eye, m_center, QVector3D(0,1,0));
    m_matrix[1].translate(-m_center);
}

void glShaderWindow::openScene()
{
    if (modelMesh) {
        delete(modelMesh);
        m_vertexBuffer.release();
        m_indexBuffer.release();
        m_colorBuffer.release();
        m_normalBuffer.release();
        m_texcoordBuffer.release();
        m_vao.release();
    }

    modelMesh = trimesh::TriMesh::read(qPrintable(modelName));
    if (!modelMesh) {
        QMessageBox::warning(0, tr("qViewer"),
                             tr("Could not load file ") + modelName, QMessageBox::Ok);
        openSceneFromFile();
    }
    modelMesh->need_bsphere();
    modelMesh->need_bbox();
    modelMesh->need_normals();
    modelMesh->need_faces();
    m_center = QVector3D(modelMesh->bsphere.center[0],
            modelMesh->bsphere.center[1],
            modelMesh->bsphere.center[2]);

	if (compute_program && caustics_program) {
        createSSBO();
    }
    bindSceneToProgram();
    initializeTransformForScene();
}

void glShaderWindow::saveScene()
{
    QFileDialog dialog(0, "Save current scene", workingDirectory,
    "*.ply *.ray *.obj *.off *.sm *.stl *.cc *.dae *.c++ *.C *.c++");
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    QString filename;
    int ret = dialog.exec();
    if (ret == QDialog::Accepted) {
        workingDirectory = dialog.directory().path();
        filename = dialog.selectedFiles()[0];
    }
    if (!filename.isNull()) {
        if (!modelMesh->write(qPrintable(filename))) {
            QMessageBox::warning(0, tr("qViewer"),
                tr("Could not save file: ") + filename, QMessageBox::Ok);
        }
    }
}

void glShaderWindow::toggleFullScreen()
{
    fullScreenSnapshots = !fullScreenSnapshots;
}

void glShaderWindow::saveScreenshot()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    QPixmap pixmap;
    if (screen) {
        if (fullScreenSnapshots) pixmap = screen->grabWindow(winId());
        else pixmap = screen->grabWindow(winId(), parent()->x(), parent()->y(), parent()->width(), parent()->height());
        // This grabs the window and the control panel
        // To get the window only:
        // pixmap = screen->grabWindow(winId(), parent()->x() + x(), parent()->y() + y(), width(), height());
    }
    QFileDialog dialog(0, "Save current picture", workingDirectory, "*.png *.jpg");
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    QString filename;
    int ret = dialog.exec();
    if (ret == QDialog::Accepted) {
        filename = dialog.selectedFiles()[0];
        if (!pixmap.save(filename)) {
            QMessageBox::warning(0, tr("qViewer"),
                tr("Could not save picture file: ") + filename, QMessageBox::Ok);
        }
    }
}

void glShaderWindow::setWindowSize(const QString& size)
{
    QStringList dims = size.split("x");
    parent()->resize(parent()->width() - width() + dims[0].toInt(), parent()->height() - height() + dims[1].toInt());
    resize(dims[0].toInt(), dims[1].toInt());
    resetFrame();
    renderNow();
}

void glShaderWindow::loadTexturesForShaders() {
    m_program->bind();
    // Erase all existing textures:
    if (texture) {
        texture->release();
        texture->destroy();
        delete texture;
        texture = 0;
    }
    if (textureNormal) {
        textureNormal->release();
        textureNormal->destroy();
        delete textureNormal;
        textureNormal = 0;
    }
    if (permTexture) {
        permTexture->release();
        permTexture->destroy();
        delete permTexture;
        permTexture = 0;
    }
    if (environmentMap) {
        environmentMap->release();
        environmentMap->destroy();
        delete environmentMap;
        environmentMap = 0;
    }
    if (causticsResult) {
        causticsResult->release();
        causticsResult->destroy();
        delete causticsResult;
        causticsResult = 0;
    }
    if (computeResult) {
        computeResult->release();
        computeResult->destroy();
        delete computeResult;
        computeResult = 0;
    }
	// Load textures as required by the shader.
	if ((m_program->uniformLocation("colorTexture") != -1)
        || (hasComputeShaders && compute_program->uniformLocation("colorTexture") != -1)) {
		glActiveTexture(GL_TEXTURE0);
        // the shader wants a texture. We load one.
        texture = new QOpenGLTexture(QImage(textureName));
        if (texture) {
            texture->setWrapMode(QOpenGLTexture::Repeat);
            texture->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
            texture->setMagnificationFilter(QOpenGLTexture::Linear);
            texture->bind(0);
        }
    }
    if ((m_program->uniformLocation("normalMap") != -1)
        || (hasComputeShaders && compute_program->uniformLocation("normalMap") != -1)) {
		glActiveTexture(GL_TEXTURE4);
        // the shader wants a texture. We load one.
        textureNormal = new QOpenGLTexture(QImage(textureNormalName));
        if (textureNormal) {
            textureNormal->setWrapMode(QOpenGLTexture::Repeat);
            textureNormal->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
            textureNormal->setMagnificationFilter(QOpenGLTexture::Linear);
            textureNormal->bind(4);
        }
        
    }
    if ((m_program->uniformLocation("envMap") != -1) 
        || (hasComputeShaders && compute_program->uniformLocation("envMap") != -1)) {
		glActiveTexture(GL_TEXTURE1);
        // the shader wants an environment map, we load one.
        environmentMap = new QOpenGLTexture(QImage(envMapName).mirrored());
        if (environmentMap) {
            environmentMap->setWrapMode(QOpenGLTexture::MirroredRepeat);
            environmentMap->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
            environmentMap->setMagnificationFilter(QOpenGLTexture::Linear);
            environmentMap->bind(1);
        }
    } 
    if ((m_program->uniformLocation("permTexture") != -1) 
        || (hasComputeShaders && compute_program->uniformLocation("permTexture") != -1)) {
        // for Perlin noise
		glActiveTexture(GL_TEXTURE6);
        permTexture = new QOpenGLTexture(QImage(pixels, 256, 256, QImage::Format_RGBA8888));

        if (permTexture) {
            permTexture->setWrapMode(QOpenGLTexture::MirroredRepeat);
            permTexture->setMinificationFilter(QOpenGLTexture::Nearest);
            permTexture->setMagnificationFilter(QOpenGLTexture::Nearest);
            permTexture->bind(6);
        }
    }
    if (hasComputeShaders) {
        // We bind the texture generated to texture unit 2 (0 is for the texture, 1 for the env map)
        // 2 is also used by the shadow map. If you need both, move one to texture unit 3.
		glActiveTexture(GL_TEXTURE2);
        computeResult = new QOpenGLTexture(QOpenGLTexture::Target2D);
        if (computeResult) {
        	computeResult->create();
            computeResult->setFormat(QOpenGLTexture::RGBA32F);
            computeResult->setSize(width(), height());
            computeResult->setWrapMode(QOpenGLTexture::MirroredRepeat);
            computeResult->setMinificationFilter(QOpenGLTexture::Nearest);
            computeResult->setMagnificationFilter(QOpenGLTexture::Nearest);
            computeResult->allocateStorage();
            computeResult->bind(2);
        }
        glActiveTexture(GL_TEXTURE5);
        causticsResult = new QOpenGLTexture(QOpenGLTexture::Target2D);
        if (causticsResult) {
        	causticsResult->create();
            causticsResult->setFormat(QOpenGLTexture::RGBA32F);
            causticsResult->setSize(256, 256);
            causticsResult->setWrapMode(QOpenGLTexture::MirroredRepeat);
            causticsResult->setMinificationFilter(QOpenGLTexture::Nearest);
            causticsResult->setMagnificationFilter(QOpenGLTexture::Nearest);
            causticsResult->allocateStorage();
            causticsResult->bind(5);
        }
    } else if (m_program->uniformLocation("shadowMap") != -1) {
    	// without Qt functions this time
		glActiveTexture(GL_TEXTURE2);
		if (shadowMap_textureId == 0) glGenTextures(1, &shadowMap_textureId);
		glBindTexture(GL_TEXTURE_2D, shadowMap_textureId);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE); // automatic mipmap generation included in OpenGL v1.4
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, shadowMapDimension, shadowMapDimension, 0, GL_DEPTH_COMPONENT, GL_FLOAT, 0);
        // create a framebuffer object, you need to delete them when program exits.
        if (shadowMap_fboId == 0) glGenFramebuffers(1, &shadowMap_fboId);
        glBindFramebuffer(GL_FRAMEBUFFER, shadowMap_fboId);

        // create a renderbuffer object to store depth info
        // NOTE: A depth renderable image should be attached the FBO for depth test.
        // If we don't attach a depth renderable image to the FBO, then
        // the rendering output will be corrupted because of missing depth test.
        // If you also need stencil test for your rendering, then you must
        // attach additional image to the stencil attachement point, too.
        if (shadowMap_rboId == 0) glGenRenderbuffers(1, &shadowMap_rboId);
        glBindRenderbuffer(GL_RENDERBUFFER, shadowMap_rboId);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, shadowMapDimension, shadowMapDimension);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);

        // attach a texture to FBO depth attachement point
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowMap_textureId, 0);

        // attach a renderbuffer to depth attachment point
        //glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER_EXT, rboId);

        //@@ disable color buffer if you don't attach any color buffer image,
        //@@ for example, rendering depth buffer only to a texture.
        //@@ Otherwise, glCheckFramebufferStatusEXT will not be complete.
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glBindTexture(GL_TEXTURE_2D, shadowMap_textureId);
	}    
    m_program->release();
}

void glShaderWindow::initialize()
{
    // Debug: which OpenGL version are we running? Must be >= 3.2 for shaders,
    // >= 4.3 for compute shaders.
    qDebug("OpenGL initialized: version: %s GLSL: %s", glGetString(GL_VERSION), glGetString(GL_SHADING_LANGUAGE_VERSION));
    // Set the clear color to black
    glClearColor( 0.2f, 0.2f, 0.2f, 1.0f );
    glEnable (GL_CULL_FACE); // cull face
    glCullFace (GL_BACK); // cull back face
    glFrontFace (GL_CCW); // GL_CCW for counter clock-wise
    glEnable (GL_DEPTH_TEST); // z_buffer
    glEnable (GL_MULTISAMPLE);

    // Prepare a complete shader program...
    // We can't call setShader because of initialization issues
    if (m_program) {
        m_program->release();
        delete(m_program);
    }
	QString shaderPath = workingDirectory + "../shaders/";
    m_program = prepareShaderProgram(shaderPath + "gpgpu_fullrt.vert", shaderPath + "gpgpu_fullrt.frag");
   	caustics_program = prepareComputeProgram(shaderPath + "gpgpu_caustics.comp");
   	compute_program = prepareComputeProgram(shaderPath + "gpgpu_fullrt.comp");

    shadowMapGenerationProgram = prepareShaderProgram(shaderPath + "h_shadowMapGeneration.vert", shaderPath + "h_shadowMapGeneration.frag");

    m_vao.create();
    m_vao.bind();
    m_vertexBuffer.create();
    m_indexBuffer.create();
    m_colorBuffer.create();
    m_normalBuffer.create();
    m_texcoordBuffer.create();
    if (width() > height()) m_screenSize = width(); else m_screenSize = height();
    initPermTexture(); // create Perlin noise texture
    m_vao.release();

    // loading texture:
    loadTexturesForShaders();

    ground_vao.create();
    ground_vao.bind();
    ground_vertexBuffer.create();
    ground_indexBuffer.create();
    ground_colorBuffer.create();
    ground_normalBuffer.create();
    ground_texcoordBuffer.create();
    ground_vao.release();
    openScene();
    createSSBO();
}

void glShaderWindow::resizeEvent(QResizeEvent* event)
{
   OpenGLWindow::resizeEvent(event);
   resize(event->size().width(), event->size().height());
}

void glShaderWindow::resize(int x, int y)
{
    OpenGLWindow::resize(x,y);
    // for GPGPU: since computeResult is the size of the window, it must be recreated at each window resize.
    if (computeResult) {
        computeResult->release();
        computeResult->destroy();
        delete computeResult;
        computeResult = 0;
    }
    if (hasComputeShaders) {
    	if (m_program == NULL) return;
        m_program->bind();
#if 0
        computeResult = new QOpenGLTexture(QImage(pixels, x, y, QImage::Format_RGBA8888));
        if (computeResult) {
            computeResult->setWrapMode(QOpenGLTexture::MirroredRepeat);
            computeResult->setMinificationFilter(QOpenGLTexture::Nearest);
            computeResult->setMagnificationFilter(QOpenGLTexture::Nearest);
            computeResult->bind(2);
        }
#else
        glActiveTexture(GL_TEXTURE2);
        computeResult = new QOpenGLTexture(QOpenGLTexture::Target2D);
        if (computeResult) {
            computeResult->create();
            computeResult->setFormat(QOpenGLTexture::RGBA32F);
            computeResult->setSize(width(), height());
            computeResult->setWrapMode(QOpenGLTexture::MirroredRepeat);
            computeResult->setMinificationFilter(QOpenGLTexture::Nearest);
            computeResult->setMagnificationFilter(QOpenGLTexture::Nearest);
            computeResult->allocateStorage();
            computeResult->bind(2);
        }
#endif
        m_program->release();
    }
    // Normal case: we rework the perspective projection for the new viewport, based on smaller size
    if (x > y) m_screenSize = x; else m_screenSize = y;
    if (m_program && modelMesh) {
        QMatrix4x4 persp;
        float radius = modelMesh->bsphere.r;
        m_program->bind();
        m_perspective.setToIdentity();
        if (x > y)
            m_perspective.perspective(60, (float)x/y, 0.1 * radius, 20 * radius);
        else {
            m_perspective.perspective((240.0/M_PI) * atan((float)y/x), (float)x/y, 0.1 * radius, 20 * radius);
        }
        resetFrame();
        renderNow();
    }
}

QOpenGLShaderProgram* glShaderWindow::prepareShaderProgram(const QString& vertexShaderPath,
                                           const QString& fragmentShaderPath)
{
    QOpenGLShaderProgram* program = new QOpenGLShaderProgram(this);
    if (!program) qWarning() << "Failed to allocate the shader";
    bool result = program->addShaderFromSourceFile(QOpenGLShader::Vertex, vertexShaderPath);
    if ( !result )
        qWarning() << program->log();
    result = program->addShaderFromSourceFile(QOpenGLShader::Fragment, fragmentShaderPath);
    if ( !result )
        qWarning() << program->log();
    result = program->link();
    if ( !result )
        qWarning() << program->log();
    return program;
}

QOpenGLShaderProgram* glShaderWindow::prepareComputeProgram(const QString& computeShaderPath)
{
    QOpenGLShaderProgram* program = new QOpenGLShaderProgram(this);
    if (!program) qWarning() << "Failed to allocate the shader";
    bool result = program->addShaderFromSourceFile(QOpenGLShader::Compute, computeShaderPath);
    if ( !result )
        qWarning() << program->log();
    result = program->link();
    if ( !result )
        qWarning() << program->log();
    else hasComputeShaders = true;
    return program;
}

void glShaderWindow::setWorkingDirectory(QString& myPath, QString& myName, QString& texture, QString& envMap)
{
    workingDirectory = myPath;
    modelName = myPath + myName;
    textureName = myPath + "../textures/" + texture;
    textureNormalName = textureName.left(textureName.size() - 4) + "_normal" + textureName.right(4);
    envMapName = myPath + "../textures/" + envMap;
}

// virtual trackball implementation
void glShaderWindow::mousePressEvent(QMouseEvent *e)
{
    lastMousePosition = (2.0/m_screenSize) * (QVector2D(e->localPos()) - QVector2D(0.5 * width(), 0.5*height()));
    mouseButton = e->button();
}

void glShaderWindow::wheelEvent(QWheelEvent * ev)
{
    int matrixMoving = 0;
    if (ev->modifiers() & Qt::ShiftModifier) matrixMoving = 1;
    else if (ev->modifiers() & Qt::AltModifier) matrixMoving = 2;

    QPoint numDegrees = ev->angleDelta() /(float) (8 * 3.0);
    if (matrixMoving == 0) {
        QMatrix4x4 t;
        t.translate(0.0, 0.0, numDegrees.y() * modelMesh->bsphere.r / 100.0);
        m_matrix[matrixMoving] = t * m_matrix[matrixMoving];
    } else  if (matrixMoving == 1) {
        lightDistance -= 0.1 * numDegrees.y();
    } else  if (matrixMoving == 2) {
        groundDistance += 0.1 * numDegrees.y();
    }
    resetFrame();
    renderNow();
}

void glShaderWindow::mouseMoveEvent(QMouseEvent *e)
{
    if (mouseButton == Qt::NoButton) return;
    QVector2D mousePosition = (2.0/m_screenSize) * (QVector2D(e->localPos()) - QVector2D(0.5 * width(), 0.5*height()));
    int matrixMoving = 0;
    if (e->modifiers() & Qt::ShiftModifier) matrixMoving = 1;
    else if (e->modifiers() & Qt::AltModifier) matrixMoving = 2;

    switch (mouseButton) {
    case Qt::LeftButton: {
        m_matrix[matrixMoving].translate(m_center); 
        float dx = mousePosition.y() - lastMousePosition.y();
        float dy = mousePosition.x() - lastMousePosition.x();
        QVector4D ax = m_matrix[matrixMoving].inverted() * QVector4D(1, 0, 0, 0);
        QVector4D ay = m_matrix[matrixMoving].inverted() * QVector4D(0, 1, 0, 0);
        QQuaternion rx = QQuaternion::fromAxisAndAngle(QVector3D(ax), dx*100);
        QQuaternion ry = QQuaternion::fromAxisAndAngle(QVector3D(ay), dy*100);
        m_matrix[matrixMoving].rotate(rx);
        m_matrix[matrixMoving].rotate(ry);
        m_matrix[matrixMoving].translate(- m_center); 
        break;
    }
    case Qt::RightButton: {
        QVector2D diff = 0.2 * m_screenSize * (mousePosition - lastMousePosition);
        if (matrixMoving == 0) {
            QMatrix4x4 t;
            t.translate(diff.x() * modelMesh->bsphere.r / 100.0, -diff.y() * modelMesh->bsphere.r / 100.0, 0.0);
            m_matrix[matrixMoving] = t * m_matrix[matrixMoving];
        } else if (matrixMoving == 1) {
            lightDistance += 0.1 * diff.y();
        } else  if (matrixMoving == 2) {
            groundDistance += 0.1 * diff.y();
        }
        break;
    }
	default: break;
    }
    lastMousePosition = mousePosition;
    resetFrame();
    renderNow();
}

void glShaderWindow::mouseReleaseEvent(QMouseEvent *e)
{
    mouseButton = Qt::NoButton;
}

QTime frameTimer;
void glShaderWindow::timerEvent(QTimerEvent *e)
{
    killTimer(e->timerId());
    int frameTime = frameTimer.restart();
    int frameRate = frameTime == 0 ? 0. : 1000 / frameTime;
    std::cout << "\rFrame Rate (last frame): " << frameRate << "FPS" << std::flush;
    renderNow();
    timer = startTimer(1);
}

void glShaderWindow::resetFrame()
{
    frame_index = 0;
}

// std::vector<float> glShaderWindow::halton_sequence(int base, int size) {
//     std::vector<float> sequence(size);
//     for (int i = 1; i <= size; ++i) {
//         float value = 0;
//         float f = 1.0;
//         int j = i;
//         while (j > 0) {
//             f = f / base;
//             value = value + f * (j % base);
//             j = j / base;
//         }
//         sequence[i-1] = value;
//     }
//     return sequence;
// }

void glShaderWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_R) {
        initializeTransformForScene();
        resetFrame();
        renderNow();
    }
}

static int nextPower2(int x) {
    // returns the first power of 2 above the argument
    // i.e. for 12 returns 4 (because 2^4 = 16 > 12)
    if (x == 0) return 1;

    x -= 1;
    x |= (x >> 1);
    x |= (x >> 2);
    x |= (x >> 4);
    x |= (x >> 8);
    x |= (x >> 16);
    x += 1;
    return x;
}



void glShaderWindow::render()
{
    QVector3D lightPosition = m_matrix[1] * (m_center + lightDistance * modelMesh->bsphere.r * QVector3D(0.5, 0.5, 1));

    QMatrix4x4 lightCoordMatrix;
    QMatrix4x4 lightPerspective;
    QMatrix4x4 mat_inverse = m_matrix[0];
    QMatrix4x4 persp_inverse = m_perspective;

    if (isGPGPU || hasComputeShaders) {
        bool invertible;
        mat_inverse = mat_inverse.inverted(&invertible);
        persp_inverse = persp_inverse.inverted(&invertible);
    }
    if (hasComputeShaders) {
               glActiveTexture(GL_TEXTURE5);
        caustics_program->bind();
        causticsResult->bind(5);
        caustics_program->setUniformValue("mat_inverse", mat_inverse);
        caustics_program->setUniformValue("persp_inverse", persp_inverse);
        caustics_program->setUniformValue("lightIntensity", QVector3D(lightIntensity[0], lightIntensity[1], lightIntensity[2]));
        caustics_program->setUniformValue("lightPosition", lightPosition);
        caustics_program->setUniformValue("transparent", transparent);
        caustics_program->setUniformValue("etaReal", QVector3D(etaReal[0], etaReal[1], etaReal[2]));
        caustics_program->setUniformValue("etaImag", QVector3D(etaImag[0], etaImag[1], etaImag[2]));
        caustics_program->setUniformValue("participatingEnv", participatingEnv);
        caustics_program->setUniformValue("radius", modelMesh->bsphere.r);
        caustics_program->setUniformValue("groundDistance", groundDistance * modelMesh->bsphere.r - m_center[1]);
        caustics_program->setUniformValue("center", m_center);
        caustics_program->setUniformValue("causticsMap", 5);
        glBindImageTexture(5, causticsResult->textureId(), 0, false, 0, GL_WRITE_ONLY, GL_RGBA32F);
        int worksize_x = 256;
        int worksize_y = 256;
        glDispatchCompute(worksize_x / compute_groupsize_x, worksize_y / compute_groupsize_y, 1);
        glBindImageTexture(5, 0, 0, false, 0, GL_READ_ONLY, GL_RGBA32F); 
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        caustics_program->release();

        // We bind the texture generated to texture unit 2 (0 is for the texture, 1 for the env map)
               glActiveTexture(GL_TEXTURE2);
        compute_program->bind();
		computeResult->bind(2);
        // Send parameters to compute program:
        compute_program->setUniformValue("center", m_center);
        compute_program->setUniformValue("radius", modelMesh->bsphere.r);
        compute_program->setUniformValue("groundDistance", groundDistance * modelMesh->bsphere.r - m_center[1]);
        compute_program->setUniformValue("mat_inverse", mat_inverse);
        compute_program->setUniformValue("persp_inverse", persp_inverse);
        compute_program->setUniformValue("lightPosition", lightPosition);
        compute_program->setUniformValue("blinnPhong", blinnPhong);
        compute_program->setUniformValue("transparent", transparent);
        compute_program->setUniformValue("lightIntensity", QVector3D(lightIntensity[0], lightIntensity[1], lightIntensity[2]));
        compute_program->setUniformValue("shininess", shininess);
        compute_program->setUniformValue("maxTrace", maxTrace);
        compute_program->setUniformValue("etaReal", QVector3D(etaReal[0], etaReal[1], etaReal[2]));
        compute_program->setUniformValue("etaImag", QVector3D(etaImag[0], etaImag[1], etaImag[2]));
        compute_program->setUniformValue("frame_index", frame_index);
        // compute_program->setUniformValueArray("halton2", halton2.data(), halton2.size(), 1);
        // compute_program->setUniformValueArray("halton3", halton3.data(), halton3.size(), 1);
        compute_program->setUniformValue("gammaCorrection", gammaCorrection);
        compute_program->setUniformValue("useNormalMap", useNormalMap);
        compute_program->setUniformValue("participatingEnv", participatingEnv);
        compute_program->setUniformValue("framebuffer", 2);
        compute_program->setUniformValue("variancebuffer", 3);
        compute_program->setUniformValue("colorTexture", 0);
        compute_program->setUniformValue("normalMap", 4);
        compute_program->setUniformValue("causticsMap", 5);
	    compute_program->setUniformValue("permTexture", 6);
        compute_program->setUniformValue("envMap", 1);
		glBindImageTexture(2, computeResult->textureId(), 0, false, 0, GL_WRITE_ONLY, GL_RGBA32F);
        worksize_x = nextPower2(width());
        worksize_y = nextPower2(height());
        glDispatchCompute(worksize_x / compute_groupsize_x, worksize_y / compute_groupsize_y, 1);
        glBindImageTexture(2, 0, 0, false, 0, GL_READ_ONLY, GL_RGBA32F); 
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        compute_program->release();
	} else if (m_program->uniformLocation("shadowMap") != -1) {
		glActiveTexture(GL_TEXTURE2);
        // The program uses a shadow map, let's compute it.
		glBindFramebuffer(GL_FRAMEBUFFER, shadowMap_fboId);
        glViewport(0, 0, shadowMapDimension, shadowMapDimension);
        // Render into shadow Map
        shadowMapGenerationProgram->bind();
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f );
        glDisable(GL_CULL_FACE); // mainly because some models intersect with the ground
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        // set up camera position in light source:
        // TODO_shadowMapping: you must initialize these two matrices.
        lightCoordMatrix.setToIdentity();
        lightPerspective.setToIdentity();

        shadowMapGenerationProgram->setUniformValue("matrix", lightCoordMatrix);
        shadowMapGenerationProgram->setUniformValue("perspective", lightPerspective);
        // Draw the entire scene:
        m_vao.bind();
        glDrawElements(GL_TRIANGLES, 3 * m_numFaces, GL_UNSIGNED_INT, 0);
        m_vao.release();
        ground_vao.bind();
        glDrawElements(GL_TRIANGLES, g_numIndices, GL_UNSIGNED_INT, 0);
        ground_vao.release();
        glFinish();
        // done. Back to normal drawing.
        shadowMapGenerationProgram->release();
		glBindFramebuffer(GL_FRAMEBUFFER, 0); // unbind
        glClearColor( 0.2f, 0.2f, 0.2f, 1.0f );
        glEnable(GL_CULL_FACE);
        glCullFace (GL_BACK); // cull back face
		glBindTexture(GL_TEXTURE_2D, shadowMap_textureId);
    }
    m_program->bind();
    const qreal retinaScale = devicePixelRatio();
    glViewport(0, 0, width() * retinaScale, height() * retinaScale);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (isGPGPU) {
        m_program->setUniformValue("computeResult", 2);
        m_program->setUniformValue("center", m_center);
        m_program->setUniformValue("mat_inverse", mat_inverse);
        m_program->setUniformValue("persp_inverse", persp_inverse);
    } else {
        m_program->setUniformValue("matrix", m_matrix[0]);
        m_program->setUniformValue("perspective", m_perspective);
        m_program->setUniformValue("lightMatrix", m_matrix[1]);
        m_program->setUniformValue("normalMatrix", m_matrix[0].normalMatrix());
    }
    m_program->setUniformValue("lightPosition", lightPosition);
    m_program->setUniformValue("blinnPhong", blinnPhong);
    m_program->setUniformValue("transparent", transparent);
    m_program->setUniformValue("lightIntensity", QVector3D(lightIntensity[0], lightIntensity[1], lightIntensity[2]));
    m_program->setUniformValue("shininess", shininess);
    m_program->setUniformValue("maxTrace", maxTrace);
    m_program->setUniformValue("etaReal", QVector3D(etaReal[0], etaReal[1], etaReal[2]));
    m_program->setUniformValue("etaImag", QVector3D(etaImag[0], etaImag[1], etaImag[2]));
    m_program->setUniformValue("frame_index", frame_index);
    // m_program->setUniformValueArray("halton2", halton2.data(), halton2.size(), 1);
    // m_program->setUniformValueArray("halton3", halton3.data(), halton3.size(), 1);
    m_program->setUniformValue("gammaCorrection", gammaCorrection);
    m_program->setUniformValue("useNormapMap", useNormalMap);
    m_program->setUniformValue("participatingEnv", participatingEnv);
    m_program->setUniformValue("radius", modelMesh->bsphere.r);
	if (m_program->uniformLocation("colorTexture") != -1) m_program->setUniformValue("colorTexture", 0);
	if (m_program->uniformLocation("normalMap") != -1) m_program->setUniformValue("normalMap", 4);
	if (m_program->uniformLocation("causticsMap") != -1) m_program->setUniformValue("caustics", 5);
    if (m_program->uniformLocation("envMap") != -1)  m_program->setUniformValue("envMap", 1);
	if (m_program->uniformLocation("permTexture") != -1)  m_program->setUniformValue("permTexture", 6);
    // Shadow Mapping
    if (m_program->uniformLocation("shadowMap") != -1) {
        m_program->setUniformValue("shadowMap", 2);
        // TODO_shadowMapping: send the right transform here
    }

    m_vao.bind();
    glDrawElements(GL_TRIANGLES, 3 * m_numFaces, GL_UNSIGNED_INT, 0);
    m_vao.release();
    m_program->release();

    frame_index++;
}
