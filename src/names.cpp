// Copyright (c) 2014 Daniel Kraft
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "names.h"

#include "leveldbwrapper.h"

#include "script/names.h"

void
CNameData::fromScript (unsigned h, const CNameScript& script)
{
  assert (script.isAnyUpdate ());
  value = script.getOpValue ();
  nHeight = h;
  addr = script.getAddress ();
}

/* ************************************************************************** */
/* CNameCache.  */

/* Try to get a name's associated data.  This looks only
   in entries, and doesn't care about deleted data.  */
bool
CNameCache::get (const valtype& name, CNameData& data) const
{
  const std::map<valtype, CNameData>::const_iterator i = entries.find (name);
  if (i == entries.end ())
    return false;

  data = i->second;
  return true;
}

/* Insert (or update) a name.  If it is marked as "deleted", this also
   removes the "deleted" mark.  */
void
CNameCache::set (const valtype& name, const CNameData& data)
{
  const std::set<valtype>::iterator di = deleted.find (name);
  if (di != deleted.end ())
    deleted.erase (di);

  const std::map<valtype, CNameData>::iterator ei = entries.find (name);
  if (ei != entries.end ())
    ei->second = data;
  else
    entries.insert (std::make_pair (name, data));
}

/* Delete a name.  If it is in the "entries" set also, remove it there.  */
void
CNameCache::remove (const valtype& name)
{
  const std::map<valtype, CNameData>::iterator ei = entries.find (name);
  if (ei != entries.end ())
    entries.erase (ei);

  deleted.insert (name);
}

/* Apply all the changes in the passed-in record on top of this one.  */
void
CNameCache::apply (const CNameCache& cache)
{
  for (std::map<valtype, CNameData>::const_iterator i = cache.entries.begin ();
       i != cache.entries.end (); ++i)
    set (i->first, i->second);

  for (std::set<valtype>::const_iterator i = cache.deleted.begin ();
       i != cache.deleted.end (); ++i)
    remove (*i);
}

/* Write all cached changes to a database batch update object.  */
void
CNameCache::writeBatch (CLevelDBBatch& batch) const
{
  for (std::map<valtype, CNameData>::const_iterator i = entries.begin ();
       i != entries.end (); ++i)
    batch.Write (std::make_pair ('n', i->first), i->second);

  for (std::set<valtype>::const_iterator i = deleted.begin ();
       i != deleted.end (); ++i)
    batch.Erase (std::make_pair ('n', *i));
}
