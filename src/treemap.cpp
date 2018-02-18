/*
 * Data structure for reasonably fast and memory efficient key/value pairs.
 * Intended for use with large token sets (map between wxString and an arbitrary type).
 * Each key can have multiple values.
 */

#include "treemap.h"
#include <wx/string.h>

//#define USE_TREE_MAP // TODO: see if we actually get a performance change

#ifdef USE_TREE_MAP
#include <algorithm>

struct TreeNode
{
    TreeNode() {}

    TreeNode(const wxString& key) : value(key) {}

    TreeNode(const wxString& key, int id) : value(key)
    {
        leaves.push_back(id);
    }

    // calling AddLeaf() after Freeze() may Thaw() parts of the tree
    void AddLeaf(TreeNode& leaf);

    // compress the data structure to (hopefully) make it more efficient
    void Freeze()
    {
        while (children.size() == 1)
        {
            if (leaves.empty())
            {
                value += children.front().value;
//#if __cplusplus >= 201103L // TODO: test if this C++11 code functions as expected
//                leaves = std::move(children.front().leaves);
//                std::vector<TreeNode> nextChildren = std::move(children.front().children);
//                children = std::move(nextChildren);
//#else
                leaves.swap(children.front().leaves);
                //children.swap(children.front().children); // This looks wrong...
                std::vector<TreeNode> nextChildren;
                nextChildren.swap(children.front().children);
                children.swap(nextChildren);
//#endif
            }
            else
                break;
        }
        for (std::vector<TreeNode>::iterator itr = children.begin();
                itr != children.end(); ++itr)
        {
            itr->Freeze();
        }
        value.Shrink();
#if __cplusplus >= 201103L
        children.shrink_to_fit();
        leaves.shrink_to_fit();
#else
        std::vector<TreeNode>(children).swap(children);
        std::vector<int>(leaves).swap(leaves);
#endif
    }

    // decompress the data structure to allow modifications
    void Thaw()
    {
        if (value.Length() > 1)
        {
            TreeNode node(value.Mid(1));
            node.children.swap(children);
            node.leaves.swap(leaves);
            value.Truncate(1);
            children.push_back(node);
        }
        for (std::vector<TreeNode>::iterator itr = children.begin();
                itr != children.end(); ++itr)
        {
            itr->Thaw();
        }
    }

    std::vector<int> GetLeaves(const wxString& key) const;

#if 0
    void Dump(wxString& out, wxString prefix = wxT("\n"))
    {
        prefix += value + wxT("-");
        for (std::vector<int>::iterator itr = leaves.begin();
                itr != leaves.end(); ++itr)
        {
            out += prefix + wxString::Format(wxT("[%d]"), *itr);
        }
        for (std::vector<TreeNode>::iterator itr = children.begin();
                itr != children.end(); ++itr)
        {
            itr->Dump(out, prefix);
        }
    }
#endif // 0

    wxString value;
    std::vector<TreeNode> children;
    std::vector<int> leaves;
};

struct TreeNodeLess
{
    bool operator() (const TreeNode& a, const TreeNode& b)
    {
        return (a.value < b.value);
    }
};

void TreeNode::AddLeaf(TreeNode& leaf)
{
    if (leaf.value.IsEmpty())
    {
        size_t len = leaves.size();
        leaves.insert(leaves.end(), leaf.leaves.begin(), leaf.leaves.end());
        std::inplace_merge(leaves.begin(), leaves.begin() + len, leaves.end());
        leaves.erase(std::unique(leaves.begin(), leaves.end()), leaves.end());
    }
    else
    {
        const wxString& suffix = leaf.value.Mid(1);
        leaf.value = leaf.value[0];
        std::vector<TreeNode>::iterator itr = std::lower_bound(children.begin(), children.end(),
                                              leaf, TreeNodeLess());
        if (itr == children.end() || itr->value[0] != leaf.value)
            itr = children.insert(itr, TreeNode(leaf.value));
        else if (itr->value.Length() > 1)
            itr->Thaw();
        leaf.value = suffix;
        itr->AddLeaf(leaf);
    }
}

std::vector<int> TreeNode::GetLeaves(const wxString& key) const
{
    if (key.IsEmpty())
        return leaves;
    std::vector<TreeNode>::const_iterator itr = std::lower_bound(children.begin(), children.end(),
            TreeNode(key[0]), TreeNodeLess());
    wxString suffix;
    if (itr == children.end() || !key.StartsWith(itr->value, &suffix))
        return std::vector<int>();
    return itr->GetLeaves(suffix);
}
#else
#include <map>

struct TreeNode
{
    std::multimap<std::string, int> leaves;
};
#endif // USE_TREE_MAP


ClTreeMap<int>::ClTreeMap() :
    m_Root(new TreeNode())
{
}

ClTreeMap<int>::ClTreeMap( const ClTreeMap<int>& other ) :
    m_Root(new TreeNode(*other.m_Root))
{

}

ClTreeMap<int>::~ClTreeMap()
{
    delete m_Root;
}

int ClTreeMap<int>::Insert(const std::string& key, int value)
{
#ifdef USE_TREE_MAP
    TreeNode leaf(key, value);
    m_Root->AddLeaf(leaf);
#else
    m_Root->leaves.insert(std::make_pair(key, value));
#endif // USE_TREE_MAP
    return value;
}

void ClTreeMap<int>::Remove(const std::string& key, int value)
{
    typedef std::multimap<std::string, int>::iterator leafItr;
    std::pair<leafItr, leafItr> rg = m_Root->leaves.equal_range(key);
    for (leafItr itr = rg.first; itr != rg.second; ++itr)
    {
        if (itr->second == value)
        {
            m_Root->leaves.erase(itr);
            return;
        }
    }
}

void ClTreeMap<int>::Remove(const std::string& key)
{
    typedef std::multimap<std::string, int>::iterator leafItr;
    std::pair<leafItr, leafItr> rg = m_Root->leaves.equal_range(key);
    for (leafItr itr = rg.first; itr != rg.second; )
    {
        leafItr eraseItr = itr;
        ++itr;
        m_Root->leaves.erase(eraseItr);
    }
}

void ClTreeMap<int>::Shrink()
{
#ifdef USE_TREE_MAP
    if (m_Root->children.size() == 1) // do not let the root node gain a value
        m_Root->children.front().Freeze();
    else
        m_Root->Freeze();
#endif // USE_TREE_MAP
}

void ClTreeMap<int>::GetIdSet(const std::string& key, std::set<int>& out_ids) const
{
#ifdef USE_TREE_MAP
    return m_Root->GetLeaves(key);
#else
    typedef std::multimap<std::string, int>::const_iterator constLeafItr;
    std::pair<constLeafItr, constLeafItr> rg = m_Root->leaves.equal_range(key);
    for (constLeafItr itr = rg.first; itr != rg.second; ++itr)
    {
        out_ids.insert(itr->second);
    }
#endif // USE_TREE_MAP
}

// Function just returns itself.
int ClTreeMap<int>::GetValue(int id) const
{
    return id;
}

int ClTreeMap<int>::GetCount() const
{
    return m_Root->leaves.size();
}

std::set<std::string> ClTreeMap<int>::GetKeySet() const
{
    std::set<std::string> retList;
    typedef std::multimap<std::string, int>::const_iterator constLeafItr;
    for (constLeafItr it = m_Root->leaves.begin(); it != m_Root->leaves.end(); ++it)
    {
        retList.insert( it->first );
    }
    return retList;
}
