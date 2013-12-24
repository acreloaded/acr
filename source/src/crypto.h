// cryptography tools

/* Based off the reference implementation of Tiger, a cryptographically
 * secure 192 bit hash function by Ross Anderson and Eli Biham. More info at:
 * http://www.cs.technion.ac.il/~biham/Reports/Tiger/
 */

#define TIGER_PASSES 3

struct tiger
{
	static const int littleendian;

	typedef unsigned long long int chunk;

	union hashval
	{
		uchar bytes[3*8];
		chunk chunks[3];
	};

	static chunk sboxes[4*256];

	static void gensboxes()
	{
		const char *str = "Tiger - A Fast New Hash Function, by Ross Anderson and Eli Biham";
		chunk state[3] = { 0x0123456789ABCDEFULL, 0xFEDCBA9876543210ULL, 0xF096A5B4C3B2E187ULL };
		uchar temp[64];

		if(!*(const char *)&littleendian) loopj(64) temp[j^7] = str[j];
		else loopj(64) temp[j] = str[j];
		loopi(1024) loop(col, 8) ((uchar *)&sboxes[i])[col] = i&0xFF;

		int abc = 2;
		loop(pass, 5) loopi(256) for(int sb = 0; sb < 1024; sb += 256)
		{
			abc++;
			if(abc >= 3) { abc = 0; compress((chunk *)temp, state); }
			loop(col, 8)
			{
				uchar val = ((uchar *)&sboxes[sb+i])[col];
				((uchar *)&sboxes[sb+i])[col] = ((uchar *)&sboxes[sb + ((uchar *)&state[abc])[col]])[col];
				((uchar *)&sboxes[sb + ((uchar *)&state[abc])[col]])[col] = val;
			}
		}
	}

	static void compress(const chunk *str, chunk state[3])
	{
		chunk a, b, c;
		chunk aa, bb, cc;
		chunk x0, x1, x2, x3, x4, x5, x6, x7;

		a = state[0];
		b = state[1];
		c = state[2];

		x0=str[0]; x1=str[1]; x2=str[2]; x3=str[3];
		x4=str[4]; x5=str[5]; x6=str[6]; x7=str[7];

		aa = a;
		bb = b;
		cc = c;

		loop(pass_no, TIGER_PASSES)
		{
			if(pass_no)
			{
				x0 -= x7 ^ 0xA5A5A5A5A5A5A5A5ULL; x1 ^= x0; x2 += x1; x3 -= x2 ^ ((~x1)<<19);
				x4 ^= x3; x5 += x4; x6 -= x5 ^ ((~x4)>>23); x7 ^= x6;
				x0 += x7; x1 -= x0 ^ ((~x7)<<19); x2 ^= x1; x3 += x2;
				x4 -= x3 ^ ((~x2)>>23); x5 ^= x4; x6 += x5; x7 -= x6 ^ 0x0123456789ABCDEFULL;
			}

#define sb1 (sboxes)
#define sb2 (sboxes+256)
#define sb3 (sboxes+256*2)
#define sb4 (sboxes+256*3)

#define round(a, b, c, x) \
	  c ^= x; \
	  a -= sb1[((c)>>(0*8))&0xFF] ^ sb2[((c)>>(2*8))&0xFF] ^ \
	   sb3[((c)>>(4*8))&0xFF] ^ sb4[((c)>>(6*8))&0xFF] ; \
	  b += sb4[((c)>>(1*8))&0xFF] ^ sb3[((c)>>(3*8))&0xFF] ^ \
	   sb2[((c)>>(5*8))&0xFF] ^ sb1[((c)>>(7*8))&0xFF] ; \
	  b *= mul;

			uint mul = !pass_no ? 5 : (pass_no==1 ? 7 : 9);
			round(a, b, c, x0) round(b, c, a, x1) round(c, a, b, x2) round(a, b, c, x3)
			round(b, c, a, x4) round(c, a, b, x5) round(a, b, c, x6) round(b, c, a, x7)

			chunk tmp = a; a = c; c = b; b = tmp;
		}

		a ^= aa;
		b -= bb;
		c += cc;

		state[0] = a;
		state[1] = b;
		state[2] = c;
	}

	static void hash(const uchar *str, int length, hashval &val)
	{
		static bool init = false;
		if(!init) { gensboxes(); init = true; }

		uchar temp[64];

		val.chunks[0] = 0x0123456789ABCDEFULL;
		val.chunks[1] = 0xFEDCBA9876543210ULL;
		val.chunks[2] = 0xF096A5B4C3B2E187ULL;

		int i = length;
		for(; i >= 64; i -= 64, str += 64)
		{
			if(!*(const char *)&littleendian)
			{
				loopj(64) temp[j^7] = str[j];
				compress((chunk *)temp, val.chunks);
			}
			else compress((chunk *)str, val.chunks);
		}

		int j;
		if(!*(const char *)&littleendian)
		{
			for(j = 0; j < i; j++) temp[j^7] = str[j];
			temp[j^7] = 0x01;
			while(++j&7) temp[j^7] = 0;
		}
		else
		{
			for(j = 0; j < i; j++) temp[j] = str[j];
			temp[j] = 0x01;
			while(++j&7) temp[j] = 0;
		}

		if(j > 56)
		{
			while(j < 64) temp[j++] = 0;
			compress((chunk *)temp, val.chunks);
			j = 0;
		}
		while(j < 56) temp[j++] = 0;
		*(chunk *)(temp+56) = (chunk)length<<3;
		compress((chunk *)temp, val.chunks);
	}
};

const int tiger::littleendian = 1;
tiger::chunk tiger::sboxes[4*256];

// This class implements the Secure Hashing Standard as defined in FIPS PUB 180-1 published April 17, 1995.
class sha1{
    private:
		unsigned H[5]; // Message digest buffers

        unsigned Length_Low; // Message length in bits
        unsigned Length_High; // Message length in bits

        unsigned char Message_Block[64]; // 512-bit message blocks
        int Message_Block_Index; // Index into message block array

        bool done; // Is the digest done?
        bool corrupt; // Is the message digest corruped?

        // Process the next 512 bits of the message
        void ProcessMessageBlock(){
			const unsigned K[] = { 0x5A827999, 0x6ED9EBA1, 0x8F1BBCDC, 0xCA62C1D6 }; // Constants defined for SHA-1
			int t;                          // Loop counter
			unsigned temp;                       // Temporary word value
			unsigned W[80];                      // Word sequence
			unsigned A, B, C, D, E;              // Word buffers

			// Initialize the first 16 words
			for(t = 0; t < 16; t++){
				W[t] = ((unsigned) Message_Block[t * 4]) << 24;
				W[t] |= ((unsigned) Message_Block[t * 4 + 1]) << 16;
				W[t] |= ((unsigned) Message_Block[t * 4 + 2]) << 8;
				W[t] |= ((unsigned) Message_Block[t * 4 + 3]);
			}

			for(t = 16; t < 80; t++) W[t] = CircularShift(1,W[t-3] ^ W[t-8] ^ W[t-14] ^ W[t-16]);

			A = H[0];
			B = H[1];
			C = H[2];
			D = H[3];
			E = H[4];

			for(t = 0; t < 20; t++){
				temp = CircularShift(5,A) + ((B & C) | ((~B) & D)) + E + W[t] + K[0];
				temp &= 0xFFFFFFFF;
				E = D;
				D = C;
				C = CircularShift(30,B);
				B = A;
				A = temp;
			}

			for(t = 20; t < 40; t++){
				temp = CircularShift(5,A) + (B ^ C ^ D) + E + W[t] + K[1];
				temp &= 0xFFFFFFFF;
				E = D;
				D = C;
				C = CircularShift(30,B);
				B = A;
				A = temp;
			}

			for(t = 40; t < 60; t++){
				temp = CircularShift(5,A) +
					   ((B & C) | (B & D) | (C & D)) + E + W[t] + K[2];
				temp &= 0xFFFFFFFF;
				E = D;
				D = C;
				C = CircularShift(30,B);
				B = A;
				A = temp;
			}

			for(t = 60; t < 80; t++){
				temp = CircularShift(5,A) + (B ^ C ^ D) + E + W[t] + K[3];
				temp &= 0xFFFFFFFF;
				E = D;
				D = C;
				C = CircularShift(30,B);
				B = A;
				A = temp;
			}

			H[0] = (H[0] + A) & 0xFFFFFFFF;
			H[1] = (H[1] + B) & 0xFFFFFFFF;
			H[2] = (H[2] + C) & 0xFFFFFFFF;
			H[3] = (H[3] + D) & 0xFFFFFFFF;
			H[4] = (H[4] + E) & 0xFFFFFFFF;

			Message_Block_Index = 0;
		}

        // Pads the current message block to 512 bits
        void PadMessage(){
			// Check to see if the current message block is too small to hold the initial padding bits and length.
			// If so, we will pad the block, process it, and then continue padding into a second block.
			Message_Block[Message_Block_Index++] = 0x80;
			if (Message_Block_Index > 56){
				while(Message_Block_Index < 64) Message_Block[Message_Block_Index++] = 0;
				ProcessMessageBlock();
			}
			while(Message_Block_Index < 56) Message_Block[Message_Block_Index++] = 0;

			// Store the message length as the last 8 octets
			Message_Block[56] = (Length_High >> 24) & 0xFF;
			Message_Block[57] = (Length_High >> 16) & 0xFF;
			Message_Block[58] = (Length_High >> 8) & 0xFF;
			Message_Block[59] = (Length_High) & 0xFF;
			Message_Block[60] = (Length_Low >> 24) & 0xFF;
			Message_Block[61] = (Length_Low >> 16) & 0xFF;
			Message_Block[62] = (Length_Low >> 8) & 0xFF;
			Message_Block[63] = (Length_Low) & 0xFF;

			ProcessMessageBlock();
		}

        // Performs a circular left shift operation
		inline unsigned CircularShift(int bits, unsigned word){ return ((word << bits) & 0xFFFFFFFF) | ((word & 0xFFFFFFFF) >> (32-bits)); }

    public:
		sha1() { Reset(); }
		virtual ~sha1() {}

        // Re-initialize the class
		void Reset(){
			Length_Low = Length_High = Message_Block_Index = 0;

			H[0] = 0x67452301;
			H[1] = 0xEFCDAB89;
			H[2]  = 0x98BADCFE;
			H[3] = 0x10325476;
			H[4] = 0xC3D2E1F0;

			done = corrupt = false;
		}

        // Returns the message digest
		bool Result(unsigned *dst){
			if (corrupt) return false;
			if (!done){
				PadMessage();
				done = true;
			}
			for(int i = 0; i < 5; i++) dst[i] = H[i];
			return true;
		}

        // Provide input to SHA1
        void Input(const unsigned char *message_array, unsigned length){
			if (!length) return;
			if (done || corrupt){
				corrupt = true;
				return;
			}

			while(length-- && !corrupt){
				Message_Block[Message_Block_Index++] = (*message_array & 0xFF);

				Length_Low += 8;
				Length_Low &= 0xFFFFFFFF;               // Force it to 32 bits
				if (Length_Low == 0)
				{
					Length_High++;
					Length_High &= 0xFFFFFFFF;          // Force it to 32 bits
					if (Length_High == 0) corrupt = true;               // Message is too long
				}

				if (Message_Block_Index == 64) ProcessMessageBlock();

				message_array++;
			}
		}
        
        void operator <<(const char *src){ // input a string
			const char *p = src;
			while(*p) Input((unsigned char *)p++, 1);
		}
		
		// input a character
        void operator <<(const char src){ Input((unsigned char *) &src, 1); }
};
