#include "OperationFactory.h"

OperationFactory* OperationFactory::instance = nullptr;

OperationFactory::OperationFactory()
    : userOp(nullptr)
    , videoOp(nullptr)
    , imageOp(nullptr)
{
}

OperationFactory::~OperationFactory()
{
    if (userOp) delete userOp;
    if (videoOp) delete videoOp;
    if (imageOp) delete imageOp;
}

OperationFactory* OperationFactory::getInstance()
{
    if (instance == nullptr) {
        instance = new OperationFactory();
    }
    return instance;
}

void* OperationFactory::CreateRepository(RepositoryType type)
{
    switch (type) {
        case USER:
            return getUserOperation();
        case VIDEO:
            return getVideoOperation();
        case IMAGE:
            return getImageOperation();
        default:
            return nullptr;
    }
}

UserOperation* OperationFactory::getUserOperation()
{
    if (userOp == nullptr) {
        userOp = new UserOperation();
    }
    return userOp;
}

VideoOperation* OperationFactory::getVideoOperation()
{
    if (videoOp == nullptr) {
        videoOp = new VideoOperation();
    }
    return videoOp;
}

ImageOperation* OperationFactory::getImageOperation()
{
    if (imageOp == nullptr) {
        imageOp = new ImageOperation();
    }
    return imageOp;
}
