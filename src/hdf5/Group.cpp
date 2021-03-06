// Copyright (c) 2013, German Neuroinformatics Node (G-Node)
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted under the terms of the BSD License. See
// LICENSE file in the root of the Project.

#include <nix/hdf5/Group.hpp>

#include <nix/util/util.hpp>

#include <nix/hdf5/ExceptionHDF5.hpp>


namespace nix {
namespace hdf5 {

optGroup::optGroup(const Group &parent, const std::string &g_name)
    : parent(parent), g_name(g_name)
{}

boost::optional<Group> optGroup::operator() (bool create) const {
    if (parent.hasGroup(g_name)) {
        g = boost::optional<Group>(parent.openGroup(g_name));
    } else if (create) {
        g = boost::optional<Group>(parent.openGroup(g_name, true));
    }
    return g;
}


Group::Group() : LocID() {}


Group::Group(hid_t hid) : LocID(hid) {}


Group::Group(const Group &other) : LocID(other) {}


bool Group::hasObject(const std::string &name) const {
    // empty string should return false, not exception (which H5Lexists would)
    if (name.empty()) {
        return false;
    }

    HTri res = H5Lexists(hid, name.c_str(), H5P_DEFAULT);
    return res.check("Group::hasObject(): H5Lexists failed");
}

bool Group::objectOfType(const std::string &name, H5O_type_t type) const {
    H5O_info_t info;

    hid_t obj = H5Oopen(hid, name.c_str(), H5P_DEFAULT);

    if (!H5Iis_valid(obj)) {
        return false;
    }

    HErr err = H5Oget_info(obj, &info);
    err.check("Could not obtain object info");

    bool res = info.type == type;

    H5Oclose(obj);
    return res;
}

ndsize_t Group::objectCount() const {
    hsize_t n_objs;
    HErr res = H5Gget_num_objs(hid, &n_objs);
    res.check("Could not get object count");
    return n_objs;
}


boost::optional<Group> Group::findGroupByAttribute(const std::string &attribute, const std::string &value) const {
    boost::optional<Group> ret;

    // look up first direct sub-group that has given attribute with given value
    for (ndsize_t index = 0; index < objectCount(); index++) {
        std::string obj_name = objectName(index);
        if(hasGroup(obj_name)) {
            Group group = openGroup(obj_name, false);
            if(group.hasAttr(attribute)) {
                std::string attr_value;
                group.getAttr(attribute, attr_value);
                if (attr_value == value) {
                    ret = group;
                    break;
                }
            }
        }
    }

    return ret;
}


boost::optional<DataSet> Group::findDataByAttribute(const std::string &attribute, const std::string &value) const {
    std::vector<DataSet> dsets;
    boost::optional<DataSet> ret;

    // look up all direct sub-datasets that have the given attribute
    for (ndsize_t index = 0; index < objectCount(); index++) {
        std::string obj_name = objectName(index);
        if(hasData(obj_name)) {
            DataSet ds = openData(obj_name);

            if (ds.hasAttr(attribute)) {
                dsets.push_back(ds);
            }
        }
    }
    // look for first dataset with given attribute set to given value
    auto found = std::find_if(dsets.begin(), dsets.end(),
                 [value, attribute](DataSet &dset) {
                     std::string attr_value;
                     dset.getAttr(attribute, attr_value);
                     return attr_value == value; });
    if(found != dsets.end()) ret = *found;

    return ret;
}


std::string Group::objectName(ndsize_t index) const {
    // check if index valid
    if(index > objectCount()) {
		//FIXME: issue #473
        throw OutOfBounds("No object at given index",
			              static_cast<size_t>(index));
    }

    std::string str_name;
    // check whether name is found by index
    ssize_t name_len = H5Lget_name_by_idx(hid,
                                                  ".",
                                                  H5_INDEX_NAME,
                                                  H5_ITER_NATIVE,
                                                  (hsize_t) index,
                                                  NULL,
                                                  0,
                                                  H5P_DEFAULT);
    if (name_len > 0) {
        char* name = new char[name_len+1];
        name_len = H5Lget_name_by_idx(hid,
                                      ".",
                                      H5_INDEX_NAME,
                                      H5_ITER_NATIVE,
                                      (hsize_t) index,
                                      name,
                                      name_len+1,
                                      H5P_DEFAULT);
        str_name = name;
        delete [] name;
    } else {
        throw H5Exception("objectName: No object found, H5Lget_name_by_idx returned no name");
    }

    return str_name;
}


bool Group::hasData(const std::string &name) const {
    return hasObject(name) && objectOfType(name, H5O_TYPE_DATASET);
}


void Group::removeData(const std::string &name) {
    if (hasData(name)) {
        HErr res = H5Gunlink(hid, name.c_str());
        res.check("Group::removeData(): Could not unlink DataSet");
    }
}

DataSet Group::createData(const std::string &name,
        DataType dtype,
        const NDSize &size) const
{
    h5x::DataType fileType = data_type_to_h5_filetype(dtype);
    return createData(name, fileType, size);
}


DataSet Group::createData(const std::string &name,
        const h5x::DataType &fileType,
        const NDSize &size,
        const NDSize &maxsize,
        NDSize chunks,
        bool max_size_unlimited,
        bool guess_chunks) const
{
    DataSpace space;

    if (size) {
        if (maxsize) {
            space = DataSpace::create(size, maxsize);
        } else {
            space = DataSpace::create(size, max_size_unlimited);
        }
    }

    BaseHDF5 dcpl = H5Pcreate(H5P_DATASET_CREATE);
    dcpl.check("Could not create data creation plist");

    if (!chunks && guess_chunks) {
        chunks = DataSet::guessChunking(size, fileType.size());
    }

    if (chunks) {
        int rank = static_cast<int>(chunks.size());
        HErr res = H5Pset_chunk(dcpl.h5id(), rank, chunks.data());
        res.check("Could not set chunk size on data set creation plist");
    }

    DataSet ds = H5Dcreate(hid, name.c_str(), fileType.h5id(), space.h5id(), H5P_DEFAULT, dcpl.h5id(), H5P_DEFAULT);
    ds.check("Group::createData: Could not create DataSet with name " + name);

    return ds;
}


DataSet Group::openData(const std::string &name) const {
    DataSet ds = H5Dopen(hid, name.c_str(), H5P_DEFAULT);
    ds.check("Group::openData(): Could not open DataSet");
    return ds;
}


bool Group::hasGroup(const std::string &name) const {
    return hasObject(name) && objectOfType(name, H5O_TYPE_GROUP);
}


Group Group::openGroup(const std::string &name, bool create) const {
    check_h5_arg_name(name);

    Group g;

    if (hasGroup(name)) {
        g = Group(H5Gopen(hid, name.c_str(), H5P_DEFAULT));
        g.check("Group::openGroup(): Could not open group: " + name);
    } else if (create) {
        BaseHDF5 gcpl = H5Pcreate(H5P_GROUP_CREATE);
        gcpl.check("Unable to create group with name '" + name + "'! (H5Pcreate)");

        //we want hdf5 to keep track of the order in which links were created so that
        //the order for indexed based accessors is stable cf. issue #387
        HErr res = H5Pset_link_creation_order(gcpl.h5id(), H5P_CRT_ORDER_TRACKED|H5P_CRT_ORDER_INDEXED);
        res.check("Unable to create group with name '" + name + "'! (H5Pset_link_cr...)");

        g = Group(H5Gcreate2(hid, name.c_str(), H5P_DEFAULT, gcpl.h5id(), H5P_DEFAULT));
        g.check("Unable to create group with name '" + name + "'! (H5Gcreate2)");

    } else {
        throw H5Exception("Unable to open group with name '" + name + "'!");
    }

    return g;
}


optGroup Group::openOptGroup(const std::string &name) {
    check_h5_arg_name(name);
    return optGroup(*this, name);
}


void Group::removeGroup(const std::string &name) {
    if (hasGroup(name))
        H5Gunlink(hid, name.c_str());
}


void Group::renameGroup(const std::string &old_name, const std::string &new_name) {
    check_h5_arg_name(new_name);

    if (hasGroup(old_name)) {
        H5Gmove(hid, old_name.c_str(), new_name.c_str()); //FIXME: H5Gmove is deprecated
    }
}


Group Group::createLink(const Group &target, const std::string &link_name) {
    check_h5_arg_name(link_name);

    HErr res = H5Lcreate_hard(target.hid, ".", hid, link_name.c_str(),
                              H5L_SAME_LOC, H5L_SAME_LOC);
    res.check("Unable to create link " + link_name);
    return openGroup(link_name, false);
}

// TODO implement some kind of roll-back in order to avoid half renamed links.
bool Group::renameAllLinks(const std::string &old_name, const std::string &new_name) {
    check_h5_arg_name(new_name);

    bool renamed = false;

    if (hasGroup(old_name)) {
        std::vector<std::string> links;

        Group  group     = openGroup(old_name, false);
        std::string gname = group.name();

        while (! gname.empty()) {
            deleteLink(gname);
            links.push_back(gname);
            gname = group.name();
        }

        renamed = links.size() > 0;
        for (std::string curr_name: links) {
            size_t pos = curr_name.find_last_of('/') + 1;

            if (curr_name.substr(pos) == old_name) {
                curr_name.replace(curr_name.begin() + pos, curr_name.end(), new_name.begin(), new_name.end());
            }

            HErr res = H5Lcreate_hard(group.hid, ".", hid, curr_name.c_str(),
                                      H5L_SAME_LOC, H5L_SAME_LOC);
            renamed = renamed && res;
        }
    }

    return renamed;
}

// TODO implement some kind of roll-back in order to avoid half removed links.
bool Group::removeAllLinks(const std::string &name) {
    bool removed = false;

    if (hasGroup(name)) {
        Group  group      = openGroup(name, false);

        std::string gname = group.name();

        while (! gname.empty()) {
            deleteLink(gname);
            gname = group.name();
        }

        removed = true;
    }

    return removed;
}


boost::optional<Group> Group::findGroupByNameOrAttribute(std::string const &attr, std::string const &value) const {

    if (hasObject(value)) {
        return boost::make_optional(openGroup(value, false));
    } else if (util::looksLikeUUID(value)) {
        return findGroupByAttribute(attr, value);
    } else {
        return boost::optional<Group>();
    }
}


boost::optional<DataSet> Group::findDataByNameOrAttribute(std::string const &attr, std::string const &value) const {

    if (hasObject(value)) {
        return boost::make_optional(openData(value));
    } else if (util::looksLikeUUID(value)) {
        return findDataByAttribute(attr, value);
    } else {
        return boost::optional<DataSet>();
    }
}


Group::~Group()
{}

} // namespace hdf5
} // namespace nix
