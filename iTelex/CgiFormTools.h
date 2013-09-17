#ifndef __CGIFORMTOOLS_H__

#define __CGIFORMTOOLS_H__

#include "iTelex/StringTab.h" // nur für TSprache

extern bool ReadConfigBool(const char *Label);

extern void CgiFormStartTabbed_P(const char *FormName);

extern void CgiFormFieldIntro_P(const char *FieldText, const char *FieldLabel);

extern void CgiFormInputFieldText_P(const char *FieldText, const char *FieldLabel, int Size, char *Value);

extern void CgiFormInputFieldULong_P(const char *FieldText, const char *FieldLabel, int Size, unsigned long Value);

extern void CgiFormCheckbox_P(const char *FieldText, const char *FieldLabel, bool Value);

extern void CgiFormDropdown_P(const char *FieldText, const char *FieldLabel, uint8_t NItems, const char **ItemList, uint8_t Value);

extern void CgiFormFinish_P(const char *ButtonText);

extern void CgiCheckText_P(struct HTTP_REQUEST * http_request, const char *FieldText, const char *FieldLabel, int Size, char *Value, TSprache Sprache);

extern unsigned long CgiCheckULong_P(struct HTTP_REQUEST * http_request, const char *FieldText, const char *FieldLabel, unsigned long Old, TSprache Sprache);

extern bool CgiCheckBool_P(struct HTTP_REQUEST * http_request, const char *FieldText, const char *FieldLabel, bool Old, TSprache Sprache);

#endif //ndef __CGIFORMTOOLS_H__