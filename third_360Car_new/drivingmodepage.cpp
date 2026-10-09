#include "drivingmodepage.h"
#include "ui_drivingmodepage.h"
#include "camerathread.h"
#include "socketconnect.h"
#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QCoreApplication>
#include <QFile>
#include <QtConcurrent>

DrivingModePage::DrivingModePage(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::DrivingModePage),
    m_frameCount(0),
    m_isRecording(false),
    m_isDriving(false),
    m_detectFrameCount(0),
    m_isUploading(false)
{
    ui->setupUi(this);
    
    // 加载车辆检测分类器
    QString cascadePath = QCoreApplication::applicationDirPath() + "/haarcascade_car.xml";
    if (!m_carClassifier.load(cascadePath.toStdString())) {
        // 尝试项目目录
        if (!m_carClassifier.load("haarcascade_car.xml")) {
            qDebug() << "[DrivingMode] 警告: 无法加载车辆检测模型 haarcascade_car.xml";
        } else {
            qDebug() << "[DrivingMode] 车辆检测模型加载成功 (当前目录)";
        }
    } else {
        qDebug() << "[DrivingMode] 车辆检测模型加载成功";
    }
    
    // 创建自动录制定时器 (每10秒触发)
    m_recordTimer = new QTimer(this);
    m_recordTimer->setInterval(10000); // 10秒
    connect(m_recordTimer, &QTimer::timeout, this, &DrivingModePage::onStartAutoRecord);
    
    // rawFrameReady 信号在 startDriving 中连接，stopDriving 中断开
}

DrivingModePage::~DrivingModePage()
{
    stopDriving();
    delete ui;
}

void DrivingModePage::startDriving()
{
    if (m_isDriving) return;
    m_isDriving = true;
    
    qDebug() << "[DrivingMode] 进入行车模式";
    
    // 打开摄像头 (设置视频源)
    CameraThread::instance()->setVideoSource("assets/cartest.mp4");
    CameraThread::instance()->openCamera();
    
    // 连接原始帧信号
    connect(CameraThread::instance(), &CameraThread::rawFrameReady,
            this, &DrivingModePage::onRawFrameReady, Qt::QueuedConnection);
    
    // 启动自动录制定时器
    m_recordTimer->start();
    
    // 立即开始第一段录制
    onStartAutoRecord();
    
    ui->lblRecStatus->setText("● 录制中");
    ui->lblRecStatus->setStyleSheet("color: #FF4444;");
}

void DrivingModePage::stopDriving()
{
    if (!m_isDriving) return;
    m_isDriving = false;
    
    qDebug() << "[DrivingMode] 退出行车模式";
    
    // 停止定时器
    m_recordTimer->stop();
    
    // 停止录制 并上传已录制的部分
    if (m_writer.isOpened()) {
        m_writer.release();
        
        // 如果已经录了一些帧，上传视频信息
        if (m_frameCount > 0 && !m_currentVideoFile.isEmpty()) {
            qDebug() << "[DrivingMode] 退出时上传部分录制 (" << m_frameCount << "帧):" << m_currentVideoFile;
            SocketConnect::instance()->uploadVideoInfo(
                QFileInfo(m_currentVideoFile).fileName(),
                m_currentVideoFile,
                ""
            );
        }
    }
    m_isRecording = false;
    m_frameCount = 0;
    
    // 断开原始帧信号
    disconnect(CameraThread::instance(), &CameraThread::rawFrameReady,
               this, &DrivingModePage::onRawFrameReady);
    
    // 延迟关闭摄像头,避免在主线程中阻塞
    CameraThread* camera = CameraThread::instance();
    QTimer::singleShot(0, camera, SLOT(closeCamera()));
    
    ui->lblRecStatus->setText("● 待机");
    ui->lblRecStatus->setStyleSheet("color: #AAA;");
    ui->lblFrameInfo->setText("帧: 0/300");
}

void DrivingModePage::onRawFrameReady(const cv::Mat& frame)
{
    if (!m_isDriving) return;
    
    // 直接在 frame 上绘制（避免每帧 clone 的开销）
    cv::Mat displayFrame = frame;  // 浅拷贝，共享数据
    
    // ========== 1. 车辆检测 (每15帧检测一次 + 降采样) ==========
    if (!m_carClassifier.empty()) {
        m_detectFrameCount++;
        if (m_detectFrameCount % 15 == 0) {
            cv::Mat gray;
            cv::Mat smallGray;
            cv::cvtColor(displayFrame, gray, cv::COLOR_BGR2GRAY);
            
            // 降采样到 0.5 倍
            double scale = 0.5;
            cv::resize(gray, smallGray, cv::Size(), scale, scale);
            cv::equalizeHist(smallGray, smallGray);
            
            std::vector<cv::Rect> smallCars;
            // 参数调整: 
            // 1.1 -> 保持不变 (缩放比例)
            // 3 -> 2 (降低邻居阈值，提高召回率，尽管可能会增加少量误检)
            // Size(20, 20) -> Size(10, 10) (在0.5倍图上检测更小的目标，对应原图20x20)
            m_carClassifier.detectMultiScale(smallGray, smallCars, 1.1, 2, 0, cv::Size(10, 10));
            
            // 恢复坐标并缓存
            m_detectedRects.clear();
            for (const auto& rect : smallCars) {
                m_detectedRects.push_back(cv::Rect(
                    rect.x / scale, 
                    rect.y / scale, 
                    rect.width / scale, 
                    rect.height / scale
                ));
            }
        }
        
        // 绘制缓存的检测框
        for (const auto& car : m_detectedRects) {
            cv::rectangle(displayFrame, car, cv::Scalar(0, 255, 0), 2); // 绿色高亮框
            cv::putText(displayFrame, "Vehicle", 
                       cv::Point(car.x, car.y - 5),
                       cv::FONT_HERSHEY_SIMPLEX, 0.5, 
                       cv::Scalar(0, 255, 0), 1);
        }
    }
    
    // ========== 2. 红绿灯检测 (从1中复用检测帧计数) ==========
    if (m_detectFrameCount % 15 == 0) {
        m_trafficLights.clear();
        
        // 仅检测上半部分 (提高性能，且红绿灯通常在高处)
        cv::Rect roiRect(0, 0, displayFrame.cols, displayFrame.rows / 2);
        cv::Mat roi = displayFrame(roiRect);
        
        cv::Mat hsv;
        cv::cvtColor(roi, hsv, cv::COLOR_BGR2HSV);
        
        // 红色掩码 (0-10, 160-180)
        cv::Mat maskRed1, maskRed2, maskRed;
        cv::inRange(hsv, cv::Scalar(0, 100, 100), cv::Scalar(10, 255, 255), maskRed1);
        cv::inRange(hsv, cv::Scalar(160, 100, 100), cv::Scalar(180, 255, 255), maskRed2);
        cv::addWeighted(maskRed1, 1.0, maskRed2, 1.0, 0.0, maskRed);
        
        // 绿色掩码 (35-85)
        cv::Mat maskGreen;
        cv::inRange(hsv, cv::Scalar(35, 100, 100), cv::Scalar(85, 255, 255), maskGreen);
        
        // 查找轮廓并过滤
        auto findLights = [&](const cv::Mat& mask, const QString& color) {
            std::vector<std::vector<cv::Point>> contours;
            cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
            
            for (const auto& contour : contours) {
                double area = cv::contourArea(contour);
                if (area < 50 || area > 5000) continue; // 面积过滤
                
                cv::Rect rect = cv::boundingRect(contour);
                double ratio = (double)rect.width / rect.height;
                
                // 形状过滤 (红绿灯通常是垂直长方形或圆形)
                if (ratio > 0.5 && ratio < 1.5) { // 接近圆形/方形
                     // 坐标还原到全图
                     rect.y += roiRect.y;
                     m_trafficLights.push_back({rect, color});
                }
            }
        };
        
        findLights(maskRed, "Red Light");
        findLights(maskGreen, "Green Light");
    }
    
    // 绘制红绿灯
    for (const auto& light : m_trafficLights) {
        cv::Scalar color = (light.color == "Red Light") ? cv::Scalar(0, 0, 255) : cv::Scalar(0, 255, 0);
        cv::rectangle(displayFrame, light.rect, color, 2);
        cv::putText(displayFrame, light.color.toStdString(), 
                   cv::Point(light.rect.x, light.rect.y - 5),
                   cv::FONT_HERSHEY_SIMPLEX, 0.5, color, 2);
    }
    
    // ========== 2. 水印叠加 (左下角) ==========
    int fontFace = cv::FONT_HERSHEY_SIMPLEX;
    double fontScale = 0.6;
    int thickness = 2;
    std::string watermark = "360 Smart Driving";
    
    int baseline = 0;
    cv::Size textSize = cv::getTextSize(watermark, fontFace, fontScale, thickness, &baseline);
    
    cv::Point textOrg(10, displayFrame.rows - 15);
    
    // 半透明背景
    cv::rectangle(displayFrame, 
                  cv::Point(textOrg.x - 2, textOrg.y - textSize.height - 4),
                  cv::Point(textOrg.x + textSize.width + 4, textOrg.y + baseline + 4),
                  cv::Scalar(0, 0, 0), cv::FILLED);
    
    // 水印文字
    cv::putText(displayFrame, watermark, textOrg, fontFace, fontScale,
                cv::Scalar(0, 191, 255), thickness); // 天蓝色
    
    // ========== 3. 自动录制 (每段300帧，含水印) ==========
    if (m_isRecording && m_writer.isOpened()) {
        // 写入 displayFrame (已包含水印和检测框)
        cv::Mat writeFrame;
        if (displayFrame.cols != 640 || displayFrame.rows != 480) {
            cv::resize(displayFrame, writeFrame, cv::Size(640, 480));
        } else {
            writeFrame = displayFrame;
        }
        m_writer.write(writeFrame);
        m_frameCount++;
        
        if (m_frameCount == 1) {
            qDebug() << "[DrivingMode] 首帧写入成功，原始尺寸:" << frame.cols << "x" << frame.rows
                     << "，写入尺寸: 640x480";
        }
        
        // 更新状态栏
        ui->lblFrameInfo->setText(QString("帧: %1/300").arg(m_frameCount));
        
        if (m_frameCount >= 300) {
            // 本段录制完成
            m_writer.release();
            m_isRecording = false;
            m_frameCount = 0;
            
            // 检查文件大小
            QFileInfo fi(m_currentVideoFile);
            qDebug() << "[DrivingMode] 录制完成一段 (300帧), 文件大小:" << fi.size() << "字节";
            
            ui->lblRecStatus->setText("● 等待下一段...");
            ui->lblRecStatus->setStyleSheet("color: #FFAA00;");
            
            // 上传视频信息
            qDebug() << "[DrivingMode] 上传视频信息: " << m_currentVideoFile;
            SocketConnect::instance()->uploadVideoInfo(
                QFileInfo(m_currentVideoFile).fileName(), // fileName
                m_currentVideoFile,                       // filePath
                ""                                        // coverPath
            );
        }
    }
    
    // ========== 4. 显示处理后的画面 ==========
    cv::Mat rgbFrame;
    cv::cvtColor(displayFrame, rgbFrame, cv::COLOR_BGR2RGB);
    QImage image(rgbFrame.data, rgbFrame.cols, rgbFrame.rows, 
                 rgbFrame.step, QImage::Format_RGB888);
    
    // 缓存当前帧用于自动上传
    m_latestFrame = displayFrame.clone();
    
    ui->lblVideoDisplay->setPixmap(QPixmap::fromImage(image).scaled(
        ui->lblVideoDisplay->size(), Qt::KeepAspectRatio, Qt::FastTransformation));
}

void DrivingModePage::onStartAutoRecord()
{
    if (!m_isDriving) return;
    
    // 触发特征图片上传 (每10秒)
    if (!m_latestFrame.empty()) {
        uploadFeatureImage(m_latestFrame);
    }
    
    if (m_isRecording) return; // 上一段还没录完，等下一个周期
    
    // 使用绝对路径创建视频保存目录
    QString videosDir = QCoreApplication::applicationDirPath() + "/videos";
    QDir dir(videosDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    
    // 以日期+时间命名 (使用绝对路径)
    QString filename = videosDir + "/" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".avi";
    m_currentVideoFile = filename;
    
    qDebug() << "[DrivingMode] 开始录制:" << filename;
    
    // 获取摄像头帧尺寸 (默认 640x480)
    int w = 640, h = 480;
    
    m_writer.open(filename.toStdString(), cv::CAP_FFMPEG,
                  cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), 
                  30, cv::Size(w, h));
    
    if (m_writer.isOpened()) {
        m_isRecording = true;
        m_frameCount = 0;
        
        ui->lblRecStatus->setText("● 录制中");
        ui->lblRecStatus->setStyleSheet("color: #FF4444;");
    } else {
        qDebug() << "[DrivingMode] 无法打开视频写入器:" << filename;
    }
}

void DrivingModePage::uploadFeatureImage(const cv::Mat& frame)
{
    if (!SocketConnect::instance()->isConnected()) return;

    // 防止重入：如果上一次还在传，这次就放弃
    if (m_isUploading) {
        qDebug() << "[DrivingMode] 上一张图片正在后台上传中，本次跳过...";
        return;
    }
    m_isUploading = true;

    // 创建截图保存目录
    QString screenshotsDir = QCoreApplication::applicationDirPath() + "/screenshots";
    QDir dir(screenshotsDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    
    // 生成文件名: feature_时间戳.jpg
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString fileName = QString("feature_%1.jpg").arg(timestamp);
    QString fullPath = screenshotsDir + "/" + fileName;
    
    // 保存截图
    if (!cv::imwrite(fullPath.toStdString(), frame)) {
        qDebug() << "[DrivingMode] 特征图片保存失败:" << fullPath;
        return;
    }
    
    qDebug() << "[DrivingMode] 自动捕获特征图片:" << fullPath;
    
    qDebug() << "[DrivingMode] 自动捕获特征图片:" << fullPath;
    
    // 使用 QtConcurrent 在后台线程执行耗时上传任务，避免阻塞 UI
    QtConcurrent::run([=](){
        // 读取文件数据用于切片上传
        QFile file(fullPath);
        if (!file.open(QIODevice::ReadOnly)) return;
        QByteArray fileData = file.readAll();
        file.close();
        
        // 计算切片数
        int sliceSize = 1024;
        int totalSlices = (fileData.size() + sliceSize - 1) / sliceSize;
        uint32_t uploadId = (uint32_t)QDateTime::currentMSecsSinceEpoch();
        
        // ===== 第1步: 发送 IMGREQ 头部包 =====
        IMGREQ req;
        memset(&req, 0, sizeof(req));
        req.user_id = SocketConnect::instance()->getUserId();
        strncpy(req.fileName, fileName.toUtf8().constData(), sizeof(req.fileName) - 1);
        strncpy(req.filePath, fullPath.toUtf8().constData(), sizeof(req.filePath) - 1);
        QByteArray timeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss").toUtf8();
        strncpy(req.createTime, timeStr.constData(), sizeof(req.createTime) - 1);
        req.photoSlices = totalSlices;
        req.uploadId = uploadId;
        
        SocketConnect::instance()->sendPacket(FID_IMG_UPLOAD, &req, sizeof(req));
        
        // 缓存文件数据，供服务器要求重传时使用
        SocketConnect::instance()->cacheUploadData(uploadId, fileData, totalSlices);
        
        // ===== 第2步: 逐片发送 PHOTOSLICE =====
        for (int i = 0; i < totalSlices; i++) {
            PHOTOSLICE slice;
            memset(&slice, 0, sizeof(slice));
            
            int offset = i * sliceSize;
            int remaining = fileData.size() - offset;
            int currentSize = qMin(remaining, sliceSize);
            
            slice.isLastSlice = (i == totalSlices - 1) ? 1 : 0;
            slice.sliceIndex = i;
            slice.sliceSize = currentSize;
            slice.uploadId = uploadId;
            memcpy(slice.buf, fileData.constData() + offset, currentSize);
            
            SocketConnect::instance()->sendPacket(FID_IMG_UPLOAD_SLICE, &slice, sizeof(slice));

            // 【关键修复】添加延时，防止发送过快导致同学服务器粘包
            QThread::msleep(5);
        }
        qDebug() << "[DrivingMode] 特征图片上传完成:" << fileName;
        
        // 任务结束，重置标志位
        m_isUploading = false;
    });
}
