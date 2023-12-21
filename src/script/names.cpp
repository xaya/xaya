// Copyright (c) 2014-2023 Daniel Kraft
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <script/names.h>

#include <uint256.h>

CNameScript::CNameScript (const CScript& script)
  : op(OP_NOP), address(script)
{
  opcodetype nameOp;
  CScript::const_iterator pc = script.begin ();
  if (!script.GetOp (pc, nameOp))
    return;

  opcodetype opcode;
  while (true)
    {
      valtype vch;

      if (!script.GetOp (pc, opcode, vch))
        return;
      if (opcode == OP_DROP || opcode == OP_2DROP || opcode == OP_NOP)
        break;
      if (!(opcode >= 0 && opcode <= OP_PUSHDATA4))
        return;

      args.push_back (vch);
    }

  // Move the pc to after any DROP or NOP.
  while (opcode == OP_DROP || opcode == OP_2DROP || opcode == OP_NOP)
    if (!script.GetOp (pc, opcode))
      break;
  pc--;

  /* Now, we have the args and the operation.  Check if we have indeed
     a valid name operation and valid argument counts.  Only now set the
     op and address members, if everything is valid.  */
  switch (nameOp)
    {
    case OP_NAME_REGISTER:
    case OP_NAME_UPDATE:
      if (args.size () != 2)
        return;
      break;

    default:
      return;
    }

  op = nameOp;
  address = CScript (pc, script.end ());
}

CScript
CNameScript::GetPrefix () const
{
  switch (op)
    {
    case OP_NAME_REGISTER:
      return CScript () << OP_NAME_REGISTER
                        << getOpName () << getOpValue ()
                        << OP_2DROP << OP_DROP;
    case OP_NAME_UPDATE:
      return CScript () << OP_NAME_UPDATE
                        << getOpName () << getOpValue ()
                        << OP_2DROP << OP_DROP;
    default:
      return CScript ();
    }
}

CScript
CNameScript::AddNamePrefix (const CScript& addr, const CScript& prefix)
{
  CScript res = prefix;
  res.insert (res.end (), addr.begin (), addr.end ());
  return res;
}

CScript
CNameScript::buildNameRegister (const CScript& addr, const valtype& name,
                                const valtype& value)
{
  CNameScript op;
  op.op = OP_NAME_REGISTER;
  op.args = {name, value};

  return AddNamePrefix (addr, op.GetPrefix ());
}

CScript
CNameScript::buildNameUpdate (const CScript& addr, const valtype& name,
                              const valtype& value)
{
  CNameScript op;
  op.op = OP_NAME_UPDATE;
  op.args = {name, value};

  return AddNamePrefix (addr, op.GetPrefix ());
}
