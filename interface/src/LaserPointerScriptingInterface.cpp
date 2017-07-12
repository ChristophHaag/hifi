//
//  LaserPointerScriptingInterface.cpp
//  interface/src
//
//  Created by Sam Gondelman 7/11/2017
//  Copyright 2017 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "LaserPointerScriptingInterface.h"

#include <QVariant>
#include "Transform.h"

LaserPointerScriptingInterface* LaserPointerScriptingInterface::getInstance() {
    static LaserPointerScriptingInterface instance;
    return &instance;
}

uint32_t LaserPointerScriptingInterface::createLaserPointer(const QVariant& properties) {
    QVariantMap propertyMap = properties.toMap();

    if (propertyMap["joint"].isValid()) {
        QString jointName = propertyMap["joint"].toString();

        Transform offsetTransform;
        if (propertyMap["offsetTransform"].isValid()) {
            // TODO:
            // convert transform
        }

        uint16_t filter = 0;
        if (propertyMap["filter"].isValid()) {
            filter = propertyMap["filter"].toUInt();
        }

        float maxDistance = 0.0f;
        if (propertyMap["maxDistance"].isValid()) {
            maxDistance = propertyMap["maxDistance"].toFloat();
        }

        bool enabled = false;
        if (propertyMap["enabled"].isValid()) {
            enabled = propertyMap["enabled"].toBool();
        }

        // TODO:
        // handle render state properties

        return LaserPointerManager::getInstance().createLaserPointer(jointName, offsetTransform, filter, maxDistance, enabled);
    } else {
        return 0;
    }
}