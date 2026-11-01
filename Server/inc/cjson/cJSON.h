#ifndef CJSON_H
#define CJSON_H

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct cJSON
    {
        struct cJSON* next;
        struct cJSON* prev;
        struct cJSON* child;
        int type;
        char* valuestring;
        int valueint;
        double valuedouble;
        char* string;
    } cJSON;

#ifdef __cplusplus
}
#endif

#endif
