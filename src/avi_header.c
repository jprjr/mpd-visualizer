#ifndef AVI_HEADER
#define AVI_HEADER
#include <stdint.h>

const uint8_t avi_header[326] = {
	0x52, 0x49, 0x46, 0x46, 0x00, 0x00, 0x00, 0x00, 0x41, 0x56, 0x49, 
	0x20, 0x4c, 0x49, 0x53, 0x54, 0x26, 0x01, 0x00, 0x00, 0x68, 0x64, 
	0x72, 0x6c, 0x61, 0x76, 0x69, 0x68, 0x38, 0x00, 0x00, 0x00, 0x35, 
	0x82, 0x00, 0x00, 0x00, 0x30, 0x2a, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x30, 0x2a, 0x00, 0x00, 0x05, 
	0x00, 0x00, 0xd0, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x4c, 0x49, 0x53, 0x54, 0x74, 0x00, 0x00, 0x00, 0x73, 0x74, 0x72, 
	0x6c, 0x73, 0x74, 0x72, 0x68, 0x38, 0x00, 0x00, 0x00, 0x76, 0x69, 
	0x64, 0x73, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 
	0x1e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x30, 0x2a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x73, 
	0x74, 0x72, 0x66, 0x28, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 
	0x00, 0x05, 0x00, 0x00, 0xd0, 0x02, 0x00, 0x00, 0x01, 0x00, 0x18, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x2a, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x4c, 0x49, 0x53, 0x54, 0x5e, 0x00, 0x00, 0x00, 
	0x73, 0x74, 0x72, 0x6c, 0x73, 0x74, 0x72, 0x68, 0x38, 0x00, 0x00, 
	0x00, 0x61, 0x75, 0x64, 0x73, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 
	0x00, 0x00, 0x00, 0x80, 0xbb, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0xee, 0x02, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x73, 0x74, 0x72, 0x66, 0x12, 0x00, 0x00, 0x00, 0x01, 
	0x00, 0x02, 0x00, 0x80, 0xbb, 0x00, 0x00, 0x00, 0xee, 0x02, 0x00, 
	0x04, 0x00, 0x10, 0x00, 0x00, 0x00, 0x4c, 0x49, 0x53, 0x54, 0x00, 
	0x00, 0x00, 0x00, 0x6d, 0x6f, 0x76, 0x69
};

const int avi_header_length = 326;

#endif
