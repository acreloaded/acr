
struct svar{
	int type;
	union{
		int mini;
		float minf;
	};
	union{
		int i;
		float f;
		char *s;
	};
	union{
		int maxi;
		float maxf;
	};
};

hashtable<const char *, svar> svars;

void addsvar(const char *name, int min, int cur, int max){
	svar id;
	id.type = ID_VAR;
	id.mini = min;
	id.i = cur;
	id.maxi = max;
	svars.access(name, id);
}
