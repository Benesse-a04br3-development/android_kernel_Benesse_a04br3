/*
 * fs/sdcardfs/inode.c
 *
 * Copyright (c) 2013 Samsung Electronics Co. Ltd
 *   Authors: Daeho Jeong, Woojoong Lee, Seunghwan Hyun,
 *               Sunghwan Yun, Sungjong Seo
 *
 * This program has been developed as a stackable file system based on
 * the WrapFS which written by
 *
 * Copyright (c) 1998-2011 Erez Zadok
 * Copyright (c) 2009     Shrikar Archak
 * Copyright (c) 2003-2011 Stony Brook University
 * Copyright (c) 2003-2011 The Research Foundation of SUNY
 *
 * This file is dual licensed.  It may be redistributed and/or modified
 * under the terms of the Apache 2.0 License OR version 2 of the GNU
 * General Public License.
 */

#include "sdcardfs.h"
#include <linux/lockdep.h>

const struct cred *override_fsids(struct sdcardfs_sb_info *sbi)
{
	struct cred * cred;
	const struct cred * old_cred;

	cred = prepare_creds();
	if (!cred)
		return NULL;

	cred->fsuid = make_kuid(&init_user_ns, sbi->options.fs_low_uid);
	cred->fsgid = make_kgid(&init_user_ns, sbi->options.fs_low_gid);

	old_cred = override_creds(cred);

	return old_cred;
}

void revert_fsids(const struct cred *old_cred)
{
	const struct cred * cur_cred;

	cur_cred = current->cred;
	revert_creds(old_cred);
	put_cred(cur_cred);
}

static int sdcardfs_create(struct inode *dir, struct dentry *dentry,
			 umode_t mode, bool want_excl)
{
	if(!check_caller_access_to_name(dir, dentry->d_name.name)) {
		printk(KERN_INFO "%s: need to check the caller's gid in packages.list\n"
						 "  dentry: %s, task:%s\n",
						 __func__, dentry->d_name.name, current->comm);
		return -EACCES;
	}
	return sdcardfs_work_dispatch_create(dir, dentry, mode, want_excl);
}

static void sdcardfs_unlink_alias(struct inode *dir, struct dentry *dentry)
{
	struct sdcardfs_sb_info *sbinfo = SDCARDFS_SB(dir->i_sb);
	struct sdcardfs_sb_info *sbinfo2;
	struct inode *inode = d_inode(dentry);
	struct inode *inode2;

	mutex_lock(&sdcardfs_super_list_lock);
	list_for_each_entry(sbinfo2, &sdcardfs_super_list, list) {
		if (sbinfo->lower_sb == sbinfo2->lower_sb && sbinfo2 != sbinfo) {
			inode2 = ilookup(sbinfo2->sb, inode->i_ino);
			if (inode2) {
				d_prune_aliases(inode2);
				iput(inode2);
			}
		}
	}
	mutex_unlock(&sdcardfs_super_list_lock);
}

static int sdcardfs_unlink(struct inode *dir, struct dentry *dentry)
{
	int err;

	if(!check_caller_access_to_name(dir, dentry->d_name.name)) {
		printk(KERN_INFO "%s: need to check the caller's gid in packages.list\n"
						 "  dentry: %s, task:%s\n",
						 __func__, dentry->d_name.name, current->comm);
		return -EACCES;
	}

	err = sdcardfs_work_dispatch_unlink(dir, dentry);

	if (!err)
		sdcardfs_unlink_alias(dir, dentry);

	return err;
}

static int sdcardfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	if(!check_caller_access_to_name(dir, dentry->d_name.name)) {
		printk(KERN_INFO "%s: need to check the caller's gid in packages.list\n"
						 "  dentry: %s, task:%s\n",
						 __func__, dentry->d_name.name, current->comm);
		return -EACCES;
	}
	return sdcardfs_work_dispatch_mkdir(dir, dentry, mode);
}

static int sdcardfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct dentry *lower_dentry;
	struct dentry *lower_dir_dentry;
	int err;
	struct path lower_path;
	const struct cred *saved_cred = NULL;

	if(!check_caller_access_to_name(dir, dentry->d_name.name)) {
		printk(KERN_INFO "%s: need to check the caller's gid in packages.list\n"
						 "  dentry: %s, task:%s\n",
						 __func__, dentry->d_name.name, current->comm);
		err = -EACCES;
		goto out_eacces;
	}

	/* save current_cred and override it */
	saved_cred = override_fsids(SDCARDFS_SB(dir->i_sb));
	if (!saved_cred)
		return -ENOMEM;

	/* sdcardfs_get_real_lower(): in case of remove an user's obb dentry
	 * the dentry on the original path should be deleted. */
	sdcardfs_get_real_lower(dentry, &lower_path);

	lower_dentry = lower_path.dentry;
	lower_dir_dentry = lock_parent(lower_dentry);

	err = vfs_rmdir(d_inode(lower_dir_dentry), lower_dentry);
	if (err)
		goto out;

	d_drop(dentry);	/* drop our dentry on success (why not VFS's job?) */
	if (d_inode(dentry))
		clear_nlink(d_inode(dentry));
	fsstack_copy_attr_times(dir, d_inode(lower_dir_dentry));
	fsstack_copy_inode_size(dir, d_inode(lower_dir_dentry));
	set_nlink(dir, d_inode(lower_dir_dentry)->i_nlink);

out:
	unlock_dir(lower_dir_dentry);
	sdcardfs_put_real_lower(dentry, &lower_path);
	revert_fsids(saved_cred);
out_eacces:
	return err;
}

/*
 * The locking rules in sdcardfs_rename are complex.  We could use a simpler
 * superblock-level name-space lock for renames and copy-ups.
 */
static int sdcardfs_rename(struct inode *old_dir, struct dentry *old_dentry,
			 struct inode *new_dir, struct dentry *new_dentry)
{
	int err = 0;
	struct dentry *lower_old_dentry = NULL;
	struct dentry *lower_new_dentry = NULL;
	struct dentry *lower_old_dir_dentry = NULL;
	struct dentry *lower_new_dir_dentry = NULL;
	struct dentry *trap = NULL;
	struct dentry *new_parent = NULL;
	struct path lower_old_path, lower_new_path;
	const struct cred *saved_cred = NULL;

	if(!check_caller_access_to_name(old_dir, old_dentry->d_name.name) ||
		!check_caller_access_to_name(new_dir, new_dentry->d_name.name)) {
		printk(KERN_INFO "%s: need to check the caller's gid in packages.list\n"
						 "  new_dentry: %s, task:%s\n",
						 __func__, new_dentry->d_name.name, current->comm);
		err = -EACCES;
		goto out_eacces;
	}

	/* save current_cred and override it */
	saved_cred = override_fsids(SDCARDFS_SB(old_dir->i_sb));
	if (!saved_cred)
		return -ENOMEM;

	sdcardfs_get_real_lower(old_dentry, &lower_old_path);
	sdcardfs_get_lower_path(new_dentry, &lower_new_path);
	lower_old_dentry = lower_old_path.dentry;
	lower_new_dentry = lower_new_path.dentry;
	lower_old_dir_dentry = dget_parent(lower_old_dentry);
	lower_new_dir_dentry = dget_parent(lower_new_dentry);

	trap = lock_rename(lower_old_dir_dentry, lower_new_dir_dentry);
	/* source should not be ancestor of target */
	if (trap == lower_old_dentry) {
		err = -EINVAL;
		goto out;
	}
	/* target should not be ancestor of source */
	if (trap == lower_new_dentry) {
		err = -ENOTEMPTY;
		goto out;
	}

	err = vfs_rename(d_inode(lower_old_dir_dentry), lower_old_dentry,
			 d_inode(lower_new_dir_dentry), lower_new_dentry,
			 NULL, 0);
	if (err)
		goto out;

	/* Copy attrs from lower dir, but i_uid/i_gid */
	sdcardfs_copy_and_fix_attrs(new_dir, d_inode(lower_new_dir_dentry));
	fsstack_copy_inode_size(new_dir, d_inode(lower_new_dir_dentry));

	if (new_dir != old_dir) {
		sdcardfs_copy_and_fix_attrs(old_dir, d_inode(lower_old_dir_dentry));
		fsstack_copy_inode_size(old_dir, d_inode(lower_old_dir_dentry));
		unlock_rename(lower_old_dir_dentry, lower_new_dir_dentry);

		/* update the derived permission of the old_dentry
		 * with its new parent
		 */
		new_parent = dget_parent(new_dentry);
		if(new_parent) {
			if(d_inode(old_dentry)) {
				update_derived_permission(old_dentry);
			}
			dput(new_parent);
		}
	} else {
		unlock_rename(lower_old_dir_dentry, lower_new_dir_dentry);
	}

	/* At this point, not all dentry information has been moved, so
	 * we pass along new_dentry for the name.*/
	get_derived_permission_new(new_dentry->d_parent, old_dentry, new_dentry);
	fix_derived_permission(d_inode(old_dentry));
	get_derive_permissions_recursive(old_dentry);
	goto out_success;

out:
	unlock_rename(lower_old_dir_dentry, lower_new_dir_dentry);

out_success:
	dput(lower_old_dir_dentry);
	dput(lower_new_dir_dentry);
	sdcardfs_put_real_lower(old_dentry, &lower_old_path);
	sdcardfs_put_lower_path(new_dentry, &lower_new_path);
	revert_fsids(saved_cred);
out_eacces:
	return err;
}

#if 0
static int sdcardfs_readlink(struct dentry *dentry, char __user *buf, int bufsiz)
{
	int err;
	struct dentry *lower_dentry;
	struct path lower_path;
	/* XXX readlink does not requires overriding credential */

	sdcardfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	if (!d_inode(lower_dentry)->i_op ||
	    !d_inode(lower_dentry)->i_op->readlink) {
		err = -EINVAL;
		goto out;
	}

	err = d_inode(lower_dentry)->i_op->readlink(lower_dentry,
						    buf, bufsiz);
	if (err < 0)
		goto out;
	fsstack_copy_attr_atime(d_inode(dentry), d_inode(lower_dentry));

out:
	sdcardfs_put_lower_path(dentry, &lower_path);
	return err;
}
#endif

#if 0
static const char *sdcardfs_follow_link(struct dentry *dentry, void **cookie)
{
	char *buf;
	int len = PAGE_SIZE, err;
	mm_segment_t old_fs;

	/* This is freed by the put_link method assuming a successful call. */
	buf = kmalloc(len, GFP_KERNEL);
	if (!buf) {
		buf = ERR_PTR(-ENOMEM);
		return buf;
	}

	/* read the symlink, and then we will follow it */
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	err = sdcardfs_readlink(dentry, buf, len);
	set_fs(old_fs);
	if (err < 0) {
		kfree(buf);
		buf = ERR_PTR(err);
	} else {
		buf[err] = '\0';
	}
	return *cookie = buf;
}
#endif

static int sdcardfs_permission(struct inode *inode, int mask)
{
	int err;

	/*
	 * Permission check on sdcardfs inode.
	 * Calling process should have AID_SDCARD_RW permission
	 */
	err = generic_permission(inode, mask);
	return err;
}

static int sdcardfs_setattr(struct dentry *dentry, struct iattr *ia)
{
	int err;
	struct dentry *lower_dentry;
	struct inode *inode;
	struct inode *lower_inode;
	struct path lower_path;
	struct iattr lower_ia;
	struct dentry *parent;

	inode = d_inode(dentry);

	/*
	 * Check if user has permission to change inode.  We don't check if
	 * this user can change the lower inode: that should happen when
	 * calling notify_change on the lower inode.
	 */
	err = inode_change_ok(inode, ia);

	/* no vfs_XXX operations required, cred overriding will be skipped. wj*/
	if (!err) {
		/* check the Android group ID */
		parent = dget_parent(dentry);
		if(!check_caller_access_to_name(d_inode(parent), dentry->d_name.name)) {
			printk(KERN_INFO "%s: need to check the caller's gid in packages.list\n"
							 "  dentry: %s, task:%s\n",
							 __func__, dentry->d_name.name, current->comm);
			err = -EACCES;
		}
		dput(parent);
	}

	if (err)
		goto out_err;

	sdcardfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	lower_inode = sdcardfs_lower_inode(inode);

	/* prepare our own lower struct iattr (with the lower file) */
	memcpy(&lower_ia, ia, sizeof(lower_ia));
	if (ia->ia_valid & ATTR_FILE)
		lower_ia.ia_file = sdcardfs_lower_file(ia->ia_file);

	lower_ia.ia_valid &= ~(ATTR_UID | ATTR_GID | ATTR_MODE);

	/*
	 * If shrinking, first truncate upper level to cancel writing dirty
	 * pages beyond the new eof; and also if its' maxbytes is more
	 * limiting (fail with -EFBIG before making any change to the lower
	 * level).  There is no need to vmtruncate the upper level
	 * afterwards in the other cases: we fsstack_copy_inode_size from
	 * the lower level.
	 */
	lockdep_off();
	if (current->mm)
		down_write(&current->mm->mmap_sem);
	if (ia->ia_valid & ATTR_SIZE) {
		err = inode_newsize_ok(inode, ia->ia_size);
		if (err) {
			if (current->mm)
				up_write(&current->mm->mmap_sem);
			lockdep_on();
			goto out;
		}
		truncate_setsize(inode, ia->ia_size);
	}
	if (current->mm)
		up_write(&current->mm->mmap_sem);

	/*
	 * mode change is for clearing setuid/setgid bits. Allow lower fs
	 * to interpret this in its own way.
	 */
	if (lower_ia.ia_valid & (ATTR_KILL_SUID | ATTR_KILL_SGID))
		lower_ia.ia_valid &= ~ATTR_MODE;

	/* notify the (possibly copied-up) lower inode */
	/*
	 * Note: we use d_inode(lower_dentry), because lower_inode may be
	 * unlinked (no inode->i_sb and i_ino==0.  This happens if someone
	 * tries to open(), unlink(), then ftruncate() a file.
	 */
	mutex_lock(&d_inode(lower_dentry)->i_mutex);
	err = notify_change(lower_dentry, &lower_ia, /* note: lower_ia */
			NULL);
	mutex_unlock(&d_inode(lower_dentry)->i_mutex);
	lockdep_on();
	if (err)
		goto out;

	/* get attributes from the lower inode and update derived permissions */
	sdcardfs_copy_and_fix_attrs(inode, lower_inode);

	/*
	 * Not running fsstack_copy_inode_size(inode, lower_inode), because
	 * VFS should update our inode size, and notify_change on
	 * lower_inode should update its size.
	 */

out:
	sdcardfs_put_lower_path(dentry, &lower_path);
out_err:
	return err;
}

static int sdcardfs_getattr(struct vfsmount *mnt, struct dentry *dentry,
		 struct kstat *stat)
{
	struct dentry *lower_dentry;
	struct inode *inode;
	struct inode *lower_inode;
	struct path lower_path;
	struct dentry *parent;

	parent = dget_parent(dentry);
	if(!check_caller_access_to_name(d_inode(parent), dentry->d_name.name)) {
		printk(KERN_INFO "%s: need to check the caller's gid in packages.list\n"
						 "  dentry: %s, task:%s\n",
						 __func__, dentry->d_name.name, current->comm);
		dput(parent);
		return -EACCES;
	}
	dput(parent);

	inode = d_inode(dentry);

	sdcardfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	lower_inode = sdcardfs_lower_inode(inode);


	sdcardfs_copy_and_fix_attrs(inode, lower_inode);
	fsstack_copy_inode_size(inode, lower_inode);


	generic_fillattr(inode, stat);
	sdcardfs_put_lower_path(dentry, &lower_path);
	return 0;
}

const struct inode_operations sdcardfs_symlink_iops = {
	.permission	= sdcardfs_permission,
	.setattr	= sdcardfs_setattr,
	/* XXX Following operations are implemented,
	 *     but FUSE(sdcard) or FAT does not support them
	 *     These methods are *NOT* perfectly tested.
	.readlink	= sdcardfs_readlink,
	.follow_link	= sdcardfs_follow_link,
	.put_link	= kfree_put_link,
	 */
};

const struct inode_operations sdcardfs_dir_iops = {
	.create		= sdcardfs_create,
	.lookup		= sdcardfs_lookup,
#if 0
	.permission	= sdcardfs_permission,
#endif
	.unlink		= sdcardfs_unlink,
	.mkdir		= sdcardfs_mkdir,
	.rmdir		= sdcardfs_rmdir,
	.rename		= sdcardfs_rename,
	.setattr	= sdcardfs_setattr,
	.getattr	= sdcardfs_getattr,
};

const struct inode_operations sdcardfs_main_iops = {
	.create		= sdcardfs_create,
	.unlink		= sdcardfs_unlink,
	.permission	= sdcardfs_permission,
	.setattr	= sdcardfs_setattr,
	.getattr	= sdcardfs_getattr,
};
