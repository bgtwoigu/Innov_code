#ifndef _SALCHANNELRS232IMP_H_
#define _SALCHANNELRS232IMP_H_


#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <string.h>
#include <unistd.h>
#include "SALTypes.h"
#include "SALChannelImp.h"


class SALChannelConfig;
class SALChannelConfigSerial;


class SALChannelImpSerial
	: public SALChannelImp
{
public:	
	static SALChannelImp* alloc ( const char* name, SALChannelConfig& Config, byte_t mode );
	SALChannelImpSerial ( const char* name, SALChannelConfigSerial* pConfig, byte_t mode );
	virtual ~SALChannelImpSerial ( void );

	//! receive packages synchronously
	bool receive ( byte_t* data, word_t* dataSize, word_t maxBytes, int32_t timeout );
	
	//! send a message synchronously
	bool send ( const byte_t* data, word_t* dataSize, int32_t timeout );


protected:	
	//! create the channel
	bool create ( void );
	bool destroy ( void );
	
private:
	const char* const mName;
	SALChannelConfigSerial* mConfig;
	int fd;
	struct termios oldPortSettings;
	struct termios newPortSettings;	
};	

#endif /*_SALCHANNELRS232IMP_H_*/
