#pragma once

#include "UserOperation.h"
#include "VideoOperation.h"
#include "ImageOperation.h"

/**
 * @brief 操作类工厂 (单例模式)
 * 
 * 用于创建不同类型的数据库操作类
 */
class OperationFactory
{
public:
    enum RepositoryType
    {
        USER,
        VIDEO,
        IMAGE
    };
    
    static OperationFactory* instance;
    static OperationFactory* getInstance();
    
    ~OperationFactory();
    
    /**
     * @brief 创建操作类实例
     * @param type 操作类型
     * @return 操作类指针，需要调用者转换类型
     */
    void* CreateRepository(RepositoryType type);
    
    // 便捷方法
    UserOperation* getUserOperation();
    VideoOperation* getVideoOperation();
    ImageOperation* getImageOperation();

private:
    OperationFactory();
    
    UserOperation* userOp;
    VideoOperation* videoOp;
    ImageOperation* imageOp;
};
