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

	void addobit(const char *sf, int cn) {
		extern int totalmillis;
		if(conlines[0].obit == cn && conlines.length() > 2){ // last one was our kill
			if(conlines[1].obit == -1){ // not 2+ kills yet
				playerent *d = getclient(cn);
				formatstring(conlines[0].line)("\f2+1 kill that was made by %s", d ? colorname(d) : "someone");
				conlines[0].obit = 1;
				addline(sf, totalmillis, cn);
			}
			else{
				playerent *d = getclient(cn);
				formatstring(conlines[1].line)("\f1%+d kills that were made by %s", ++conlines[1].obit, d ? colorname(d) : "someone");
				conlines[1].millis = totalmillis;
				// overwrite
				copystring(conlines[0].line, sf);
				conlines[0].millis = totalmillis;
			}
		}
		else{
			conlines[0].obit = -1; // force no overwrite
			addline(sf, totalmillis, cn);
		}
		conlines[0].obit = cn;
	}

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

