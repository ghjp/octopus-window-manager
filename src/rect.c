#define G_LOG_DOMAIN "rect"
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "octopus.h"
#define SWAP(tmp, a, b)	G_STMT_START {	\
	(tmp) = (a);		\
	(a) = (b);		\
	(b) = (tmp);		\
} G_STMT_END

/* find intersection length of two lines */
G_INLINE_FUNC gint lineisect(gint p1, gint l1, gint p2, gint l2)
{
	/* make sure p1 <= p2 */
	if (p1 > p2) {
    gint tmp;
		SWAP(tmp, p1, p2);
		SWAP(tmp, l1, l2);
	}

	/* calculate the intersection */
	if (p1 + l1 < p2)
		return 0;
	else if (p2 + l2 < p1 + l1)
		return l2;
	else
		return p1 + l1 - p2;
}

/*
 * find the area of the intersection of two
 * rectangles.
 */
gint rect_intersection(rect_t *r1, rect_t *r2)
{
	gint xsect, ysect;

	/*
	 * find intersection of lines in the x and
	 * y directions, multiply for the area of
	 * the intersection rectangle.
	 */
	xsect = lineisect(r1->x1, r1->x2 - r1->x1, r2->x1,
		r2->x2 - r2->x1);
  if(!xsect)
    return 0;
	ysect = lineisect(r1->y1, r1->y2 - r1->y1, r2->y1,
		r2->y2 - r2->y1);

	return ysect * xsect;
}
