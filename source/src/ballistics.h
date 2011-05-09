static inline bool intersectbox(const vec &o, const vec &rad, const vec &from, const vec &to, vec *end){ // if line segment hits entity bounding box
	const vec *p;
	vec v = to, w = o;
	v.sub(from);
	w.sub(from);
	float c1 = w.dot(v);

	if(c1<=0) p = &from;
	else
	{
		float c2 = v.squaredlen();
		if(c2<=c1) p = &to;
		else
		{
			float f = c1/c2;
			v.mul(f).add(from);
			p = &v;
		}
	}

	if(p->x <= o.x+rad.x
	   && p->x >= o.x-rad.x
	   && p->y <= o.y+rad.y
	   && p->y >= o.y-rad.y
	   && p->z <= o.z+rad.z
	   && p->z >= o.z-rad.z)
	{
		if(end) *end = *p;
		return true;
	}
	return false;
}

static inline bool intersectsphere(const vec &from, const vec &to, vec center, float radius, float &dist){
	vec ray(to);
	ray.sub(from);
	center.sub(from);
	float v = center.dot(ray),
		  inside = radius*radius - center.squaredlen();
	if(inside < 0 && v < 0) return false;
	float raysq = ray.squaredlen(), d = inside*raysq + v*v;
	if(d < 0) return false;
	dist = (v - sqrtf(d)) / raysq;
	return dist >= 0 && dist <= 1;
}

static inline bool intersectcylinder(const vec &from, const vec &to, const vec &start, const vec &end, float radius, float &dist){
	vec d(end), m(from), n(to);
	d.sub(start);
	m.sub(start);
	n.sub(from);
	float md = m.dot(d),
		  nd = n.dot(d),
		  dd = d.squaredlen();
	if(md < 0 && md + nd < 0) return false;
	if(md > dd && md + nd > dd) return false;
	float nn = n.squaredlen(),
		  mn = m.dot(n),
		  a = dd*nn - nd*nd,
		  k = m.squaredlen() - radius*radius,
		  c = dd*k - md*md;
	if(fabs(a) < 0.005f)
	{
		if(c > 0) return false;
		if(md < 0) dist = -mn / nn;
		else if(md > dd) dist = (nd - mn) / nn;
		else dist = 0;
		return true;
	}
	float b = dd*mn - nd*md,
		  discrim = b*b - a*c;
	if(discrim < 0) return false;
	dist = (-b - sqrtf(discrim)) / a;
	float offset = md + dist*nd;
	if(offset < 0)
	{
		if(nd < 0) return false;
		dist = -md / nd;
		if(k + dist*(2*mn + dist*nn) > 0) return false;
	}
	else if(offset > dd)
	{
		if(nd >= 0) return false;
		dist = (dd - md) / nd;
		if(k + dd - 2*md + dist*(2*(mn-nd) + dist*nn) > 0) return false;
	}
	return dist >= 0 && dist <= 1;
}

static inline bool inplayer(const vec &location, const vec &target, float above, float below, float radius){
	// check for z
	if(location.z > target.z + above || target.z > target.z + below) return false;
	// check for xy
	return radius > target.distxy(location);
}