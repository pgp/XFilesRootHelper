#ifndef _COMPRESS_RQ_OPTIONS_H_
#define _COMPRESS_RQ_OPTIONS_H_

#include "../desc/IDescriptor.h"

typedef struct {
	uint8_t compressionLevel;
	uint8_t encryptHeader;
	uint8_t solid;
} compress_rq_options_t;

void readcompress_rq_options(IDescriptor& fd, compress_rq_options_t& dataContainer) {
	fd.read(&(dataContainer.compressionLevel),sizeof(uint8_t));
	fd.read(&(dataContainer.encryptHeader),sizeof(uint8_t));
	fd.read(&(dataContainer.solid),sizeof(uint8_t));
}

#endif /* _COMPRESS_RQ_OPTIONS_H_ */
