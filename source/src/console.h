struct cline { char *line; int millis; int obit; };

template<class LINE> struct consolebuffer
{
	int maxlines;
	vector<LINE> conlines;

	consolebuffer(int maxlines = 100) : maxlines(maxlines) {}

	LINE &addline(const char *sf, int millis, int obit=-1)		// add a line to the console buffer
	{
		LINE cl;
		cl.line = conlines.length()>maxlines ? conlines.pop().line : newstringbuf("");   // constrain the buffer size
		cl.millis = millis;						// for how long to keep line on screen
		cl.obit = obit;
		copystring(cl.line, sf);
		return conlines.insert(0, cl);
	}

	void addline(const char *sf) { extern int totalmillis; addline(sf, totalmillis); }

	void setmaxlines(int numlines)
	{
		maxlines = numlines;
		while(conlines.length() > maxlines) delete[] conlines.pop().line;
	}
		
	virtual ~consolebuffer() 
	{
		while(conlines.length()) delete[] conlines.pop().line;
	}

	virtual void render() = 0;
};

struct textinputbuffer
{
	string buf;
	int pos, max;

	textinputbuffer();

	bool key(int code, bool isdown, int unicode);
};

