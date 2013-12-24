struct cline { char *line; int millis; void cleanup(){ delete[] line; } };

template<class LINE> struct consolebuffer
{
	int maxlines, fullconsole;
	vector<LINE> conlines;

	void toggleconsole()
	{
		extern int altconsize;
		if(!fullconsole) fullconsole = altconsize ? 1 : 2;
		else fullconsole = ++fullconsole % 3;
	}

	consolebuffer(int maxlines = 100) : maxlines(maxlines), fullconsole(0) {}

	LINE &addline(const char *sf, int millis)		// add a line to the console buffer
	{
		LINE cl;
		// constrain the buffer size
		if(conlines.length()>maxlines) conlines.pop();
		cl.line = newstringbuf("");
		cl.millis = millis;						// for how long to keep line on screen
		copystring(cl.line, sf);
		return conlines.insert(0, cl);
	}

	void addline(const char *sf) { extern int totalmillis; addline(sf, totalmillis); }

	void setmaxlines(int numlines)
	{
		maxlines = numlines;
		while(conlines.length() > maxlines) conlines.pop().cleanup();
	}
		
	virtual ~consolebuffer() 
	{
		setmaxlines(0);
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

