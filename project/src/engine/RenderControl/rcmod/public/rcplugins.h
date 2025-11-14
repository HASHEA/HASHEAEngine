/*
    filename:       rcplugins.h
    author:         Ming Dong
    date:           2016-Aug-13
    description:    
*/
#pragma once

class RCPluginInterface
{
public:
    virtual const char* getPluginName() const = 0;
//    virtual bool        init() = 0;
//    virtual bool        uninit() = 0;
};
