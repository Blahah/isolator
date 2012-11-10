
#ifndef ISOLATOR_TRANSCRIPTS_HPP
#define ISOLATOR_TRANSCRIPTS_HPP

#include <boost/flyweight.hpp>
#include <cstdio>
#include <set>
#include <string>

#include "common.hpp"
#include "intervals.hpp"

/* An exon, relative to some transcript. */
struct Exon
{
    Exon();
    Exon(pos_t, pos_t);

    pos_t start;
    pos_t end;

    /* Order by (start, then end) position. */
    bool operator < (const Exon&) const;
};


/* A transcript is an ordered set of exons with an associated sequence name,
 * strand, etc.
 */
class Transcript : public std::set<Exon>
{
    public:
        Transcript();
        Transcript(const Transcript& other);
        ~Transcript();

        bool operator < (const Transcript& other) const;

        bool overlaps(SeqName seqname, pos_t start, pos_t end) const;

        /* Insert a new exon. Use this rather than the std::set insert functions
         * as this will update min_start, max_end. */
        void add(pos_t start, pos_t end);

        GeneID gene_id;
        TranscriptID transcript_id;
        SeqName seqname;

        strand_t strand;

        pos_t min_start;
        pos_t max_end;

        pos_t start_codon;
        pos_t stop_codon;

        /* A sequential identifier, unique within the container TranscriptSet. */
        unsigned int id;

    private:
        friend class TranscriptSet;
};


/* A set of transcripts. */
class TranscriptSet
{
    public:
        TranscriptSet();

        /* Read transcripts form a GTF file into the set.
         *
         * Args:
         *   f: A file, opened for reading, containg GTF data.
         */
        void read_gtf(FILE* f);

        /* Number of transcripts held in the set. */
        size_t size() const;

        /* Fill a vector with all genomic regions which for every trancsript are
         * either entirely exonic or non-overlapping. */
        void get_consensus_exonic(std::vector<Interval>&);

        /* Fill a vector with intergenic regions, including those at the very
         * beginnings or ends of chromosomes. */
        void get_intergenic(std::vector<Interval>&);


    private:
        /* Transcripts ordered by position. */
        std::set<Transcript> transcripts;

        friend class TranscriptSetLocusIterator;
};


/* A subset of a parent TranscriptSet consisting of all transcripts within a
 * particular locus. */
class TranscriptSetLocus : public std::deque<Transcript>
{
    public:
        TranscriptSetLocus();
        TranscriptSetLocus(const TranscriptSetLocus& other);

        void push_back(Transcript const& t);
        void clear();

        SeqName seqname;
        pos_t min_start;
        pos_t max_end;
};


/* Iterate over overlapping clumps of transcripts.
 *
 * More precisely:
 * Consider an undirected graph where each transcript is a node and there is an
 * edge between two transcripts iff their exonic portions overlap at all. This
 * classs iterates over connected components within that graph.
 */
class TranscriptSetLocusIterator :
    public boost::iterator_facade<TranscriptSetLocusIterator,
                                  const TranscriptSetLocus,
                                  boost::forward_traversal_tag>
{
    public:
        TranscriptSetLocusIterator();
        TranscriptSetLocusIterator(const TranscriptSet&);

    private:
        friend class boost::iterator_core_access;

        void increment();
        bool equal(const TranscriptSetLocusIterator& other) const;
        const TranscriptSetLocus& dereference() const;

        TranscriptSetLocus locus;
        const TranscriptSet* ts;
        std::set<Transcript>::const_iterator i;
};


#endif


