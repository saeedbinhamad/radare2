/* radare - LGPL - Copyright 2010 - nibble<develsec.org> */

#include <stdio.h>
#include <string.h>
#include <r_anal.h>
#include <r_diff.h>
#include <r_list.h>
#include <r_util.h>
#include <r_core.h>

#define THRESHOLDFCN 0.7F
#define THRESHOLDBB 0.7F

static ut8* gdiff_fingerprint(RAnal *a, ut8* buf, int len) {
	RAnalOp *aop;
	ut8 *ret = NULL;
	int oplen, idx = 0;

	if (!(ret = malloc (len)))
		return NULL;
	memcpy (ret, buf, len);
	if (!(aop = r_anal_aop_new ())) {
		free (ret);
		return NULL;
	}
	while (idx < len) {
		if ((oplen = r_anal_aop (a, aop, 0, buf+idx, len-idx)) == 0)
			break;
		if (aop->nopcode != 0)
			memset (ret+idx+aop->nopcode, 0, oplen-aop->nopcode);
		idx += oplen;
	}
	free (aop);
	return ret;
}

static int concat_fcn_fp(RCore *c, RAnalFcn *fcn) {
	RAnalBlock *bb;
	RListIter *iter;
	int len = 0;
	
	iter = r_list_iterator (fcn->bbs), fcn->fingerprint = NULL;
	while (r_list_iter_next (iter)) {
		bb = r_list_iter_get (iter);
		len += bb->size;
		fcn->fingerprint = realloc (fcn->fingerprint, len);
		if (!fcn->fingerprint)
			return 0;
		memcpy (fcn->fingerprint+len-bb->size, bb->fingerprint, bb->size);
	}
	return len;
}

static void gdiff_diff_bb(RAnalFcn *mfcn, RAnalFcn *mfcn2) {
	RAnalBlock *bb, *bb2, *mbb, *mbb2;
	RListIter *iter, *iter2;
	double t, ot;

	mfcn->diff->type = mfcn2->diff->type = R_ANAL_DIFF_TYPE_MATCH;
	iter = r_list_iterator (mfcn->bbs);
	while (r_list_iter_next (iter)) {
		bb = r_list_iter_get (iter);
		if (bb->diff->type != R_ANAL_DIFF_TYPE_NULL)
			continue;
		ot = 0;
		mbb = mbb2 = NULL;
		iter2 = r_list_iterator (mfcn2->bbs);
		while (r_list_iter_next (iter2)) {
			bb2 = r_list_iter_get (iter2);
			if (bb2->diff->type == R_ANAL_DIFF_TYPE_NULL) {
				r_diff_buffers_distance (NULL, bb->fingerprint, bb->size,
						bb2->fingerprint, bb2->size, NULL, &t);
#if 0
				eprintf ("BB: %llx - %llx => %lli - %lli => %f\n", bb->addr, bb2->addr,
						bb->size, bb->size, t);
#endif 
				if (t > THRESHOLDBB && t > ot) {
					ot = t;
					mbb = bb;
					mbb2 = bb2;
					if (t == 1) break;
				}
			}
		}
		if (mbb != NULL && mbb2 != NULL) {
			if (ot == 1)
				mbb->diff->type = mbb2->diff->type = R_ANAL_DIFF_TYPE_MATCH;
			else {
				mbb->diff->type = mbb2->diff->type = R_ANAL_DIFF_TYPE_UNMATCH;
				mfcn->diff->type = mfcn2->diff->type = R_ANAL_DIFF_TYPE_UNMATCH;
			}
			R_FREE (mbb->fingerprint);
			R_FREE (mbb2->fingerprint);
			mbb->diff->addr = mbb2->addr;
			mbb2->diff->addr = mbb->addr;
		} else
			mfcn->diff->type = mfcn2->diff->type = R_ANAL_DIFF_TYPE_UNMATCH;
	}
}

static void gdiff_diff_fcn(RList *fcns, RList *fcns2) {
	RAnalFcn *fcn, *fcn2, *mfcn, *mfcn2;
	RListIter *iter, *iter2;
	ut64 maxsize, minsize;
	double t, ot;

	/* Compare functions with the same name */
	iter = r_list_iterator (fcns);
	while (r_list_iter_next (iter)) {
		fcn = r_list_iter_get (iter);
		if (fcn->type != R_ANAL_FCN_TYPE_FCN || fcn->name == NULL ||
			(strncmp (fcn->name, "sym.", 4) && strncmp (fcn->name, "fcn.sym.", 8) &&
			strncmp (fcn->name, "imp.", 4) && strncmp (fcn->name, "fcn.imp.", 8)))
			continue;
		iter2 = r_list_iterator (fcns2);
		while (r_list_iter_next (iter2)) {
			fcn2 = r_list_iter_get (iter2);
			if (fcn2->type != R_ANAL_FCN_TYPE_FCN || fcn2->name == NULL || 
				(strncmp (fcn->name, "sym.", 4) && strncmp (fcn->name, "fcn.sym.", 8) &&
				strncmp (fcn->name, "imp.", 4) && strncmp (fcn->name, "fcn.imp.", 8)) ||
				strcmp (fcn->name, fcn2->name))
				continue;
			r_diff_buffers_distance (NULL, fcn->fingerprint, fcn->size,
					fcn2->fingerprint, fcn2->size, NULL, &t);
#if 1
			eprintf ("FCN NAME (NAME): %s - %s => %lli - %lli => %f\n", fcn->name, fcn2->name,
					fcn->size, fcn2->size, t);
#endif 
			/* Set flag in matched functions */
			if (t == 1)
				fcn->diff->type = fcn2->diff->type = R_ANAL_DIFF_TYPE_MATCH;
			else
				fcn->diff->type = fcn2->diff->type = R_ANAL_DIFF_TYPE_UNMATCH;
			R_FREE (fcn->fingerprint);
			R_FREE (fcn2->fingerprint);
			fcn->diff->addr = fcn2->addr;
			fcn2->diff->addr = fcn->addr;
			R_FREE (fcn->diff->name);
			if (fcn2->name)
				fcn->diff->name = strdup (fcn2->name);
			R_FREE (fcn2->diff->name);
			if (fcn->name)
				fcn2->diff->name = strdup (fcn->name);
			gdiff_diff_bb (fcn, fcn2);
			break;
		}
	}
	/* Compare remaining functions */
	iter = r_list_iterator (fcns);
	while (r_list_iter_next (iter)) {
		fcn = r_list_iter_get (iter);
		if (fcn->type != R_ANAL_FCN_TYPE_FCN || fcn->diff->type != R_ANAL_DIFF_TYPE_NULL)
			continue;
		ot = 0;
		mfcn = mfcn2 = NULL;
		iter2 = r_list_iterator (fcns2);
		while (r_list_iter_next (iter2)) {
			fcn2 = r_list_iter_get (iter2);
			if (fcn->size > fcn2->size) {
				maxsize = fcn->size;
				minsize = fcn2->size;
			} else {
				maxsize = fcn2->size;
				minsize = fcn->size;
			}
			if (fcn2->type != R_ANAL_FCN_TYPE_FCN || fcn2->diff->type != R_ANAL_DIFF_TYPE_NULL ||
				(maxsize * THRESHOLDFCN > minsize))
				continue;
			r_diff_buffers_distance (NULL, fcn->fingerprint, fcn->size,
					fcn2->fingerprint, fcn2->size, NULL, &t);
#if 1
			eprintf ("FCN: %s - %s => %lli - %lli => %f\n", fcn->name, fcn2->name,
					fcn->size, fcn2->size, t);
#endif 
			if (t > THRESHOLDFCN && t > ot) {
				ot = t;
				mfcn = fcn;
				mfcn2 = fcn2;
				if (t == 1) break;
			}
		}
		if (mfcn != NULL && mfcn2 != NULL) {
#if 0
			eprintf ("Match => %s - %s\n", mfcn->name, mfcn2->name);
#endif
			/* Set flag in matched functions */
			if (ot == 1)
				mfcn->diff->type = mfcn2->diff->type = R_ANAL_DIFF_TYPE_MATCH;
			else
				mfcn->diff->type = mfcn2->diff->type = R_ANAL_DIFF_TYPE_UNMATCH;
			R_FREE (mfcn->fingerprint);
			R_FREE (mfcn2->fingerprint);
			mfcn->diff->addr = mfcn2->addr;
			mfcn2->diff->addr = mfcn->addr;
			R_FREE (mfcn->diff->name);
			if (mfcn2->name)
				mfcn->diff->name = strdup (mfcn2->name);
			R_FREE (mfcn2->diff->name);
			if (mfcn->name)
				mfcn2->diff->name = strdup (mfcn->name);
			gdiff_diff_bb (mfcn, mfcn2);
		}
	}
}

R_API int r_core_gdiff(RCore *c, RCore *c2) {
	RCore *cores[2] = {c, c2};
	RAnalFcn *fcn;
	RAnalBlock *bb;
	RListIter *iter, *iter2;
	ut8 *buf;
	int i;

	for (i = 0; i < 2; i++) {
		r_core_anal_all (cores[i]);
		/* Fingerprint fcn bbs */
		iter = r_list_iterator (cores[i]->anal->fcns);
		while (r_list_iter_next (iter)) {
			fcn = r_list_iter_get (iter);
			iter2 = r_list_iterator (fcn->bbs);
			while (r_list_iter_next (iter2)) {
				bb = r_list_iter_get (iter2);
				if ((buf = malloc (bb->size))) {
					if (r_io_read_at (cores[i]->io, bb->addr, buf, bb->size) == bb->size) {
						bb->fingerprint = gdiff_fingerprint (cores[i]->anal, buf, bb->size);
					}
					free (buf);
				}
			}
		}
		/* Concat bb fingerprints per fcn */
		iter = r_list_iterator (cores[i]->anal->fcns);
		while (r_list_iter_next (iter)) {
			fcn = r_list_iter_get (iter);
			fcn->size = concat_fcn_fp (cores[i], fcn);
		}
	}
	/* Diff functions */
	gdiff_diff_fcn (cores[0]->anal->fcns, cores[1]->anal->fcns);

	return R_TRUE;
}
